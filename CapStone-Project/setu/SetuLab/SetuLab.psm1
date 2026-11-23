# SetuLab.psm1 - CNNP Network Studio lab commands for Windows (PowerShell 7+)
# Every lab action is a PowerShell cmdlet. The virtual routers and switches run as
# Linux containers inside WSL2; this module talks to them for you.
#   Import-Module C:\setu\SetuLab\SetuLab.psm1
#   Get-Command -Module SetuLab

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Distro = if ($env:SETU_DISTRO) { $env:SETU_DISTRO } else { 'Ubuntu-24.04' }
$script:Root   = Split-Path -Parent $PSScriptRoot
$script:Lab    = if ($env:SETU_LAB) { $env:SETU_LAB } else { 'setu-w01' }

# ---------------------------------------------------------------- helpers
function ConvertTo-SetuWslPath {
    <# C:\setu\captures\a.pcap  ->  /mnt/c/setu/captures/a.pcap #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Path)
    $full = if ($Path -match '^[A-Za-z]:\\') { $Path } else { [IO.Path]::GetFullPath($Path) }
    if ($full -match '^([A-Za-z]):\\(.*)$') {
        return '/mnt/' + $Matches[1].ToLower() + '/' + ($Matches[2] -replace '\\', '/')
    }
    return ($full -replace '\\', '/')
}

function ConvertTo-SetuB64([string]$Text) {
    [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($Text -replace "`r", '')))
}

function Invoke-SetuWsl {
    <# Runs a command in the lab engine (WSL2, as root). Base64 transport means no quoting problems. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Command, [switch]$AllowFailure)
    Write-Verbose "engine> $Command"
    $b64 = ConvertTo-SetuB64 $Command
    $out = & wsl.exe -d $script:Distro -u root -- bash -c "echo $b64 | base64 -d | bash" 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0 -and -not $AllowFailure) {
        throw "Lab engine command failed (exit $code): $Command`n$($out -join [Environment]::NewLine)"
    }
    return $out
}

function Get-SetuContainer([string]$Node, [string]$Lab = $script:Lab) { "clab-$Lab-$Node" }

function Invoke-SetuNode {
    <# Runs a command on a lab device (router, switch, host). This is the device's own CLI. #>
    [CmdletBinding()] param(
        [Parameter(Mandatory)][string]$Node,
        [Parameter(Mandatory)][string]$Command,
        [string]$Lab = $script:Lab,
        [switch]$AllowFailure)
    $c = Get-SetuContainer $Node $Lab
    $b64 = ConvertTo-SetuB64 $Command
    Invoke-SetuWsl -Command "docker exec $c sh -c 'echo $b64 | base64 -d | sh'" -AllowFailure:$AllowFailure
}

function Get-SetuPercentile {
    <# Nearest-rank percentile of a numeric array. #>
    param([double[]]$Values, [double]$P)
    if (-not $Values -or $Values.Count -eq 0) { return [double]::NaN }
    $s = $Values | Sort-Object
    $rank = [Math]::Ceiling($P / 100.0 * $s.Count)
    if ($rank -lt 1) { $rank = 1 }
    return [double]$s[[int]$rank - 1]
}

function New-SetuNetemArgs {
    param([double]$DelayMs, [double]$JitterMs, [double]$LossPct, [double]$LossCorrelationPct,
          [double]$RateMbit, [int]$LimitPackets, [double]$ReorderPct)
    $a = @()
    if ($DelayMs -gt 0) {
        $a += "delay ${DelayMs}ms"
        if ($JitterMs -gt 0) { $a += "${JitterMs}ms distribution normal" }
    }
    if ($ReorderPct -gt 0) { $a += "reorder ${ReorderPct}% 50%" }
    if ($LossPct -gt 0) {
        $a += "loss ${LossPct}%"
        if ($LossCorrelationPct -gt 0) { $a += "${LossCorrelationPct}%" }
    }
    if ($RateMbit -gt 0) { $a += "rate ${RateMbit}mbit" }
    if ($LimitPackets -gt 0) { $a += "limit $LimitPackets" }
    return ($a -join ' ')
}

# ---------------------------------------------------------------- setup
function Install-SetuPrerequisites {
    <# Week 0. Installs Windows tools, WSL2 + Ubuntu, Docker and containerlab. Run as Administrator. #>
    [CmdletBinding()] param([switch]$SkipWindowsApps)
    if (-not $SkipWindowsApps) {
        $apps = 'WiresharkFoundation.Wireshark','Microsoft.VisualStudioCode','Git.Git',
                'Python.Python.3.12','Kitware.CMake','Microsoft.WindowsTerminal'
        foreach ($id in $apps) {
            Write-Host "winget install $id"
            winget install --id $id -e --accept-source-agreements --accept-package-agreements | Out-Host
        }
        Write-Host 'C/C++ compiler: Visual Studio 2022 Build Tools with the C++ workload'
        winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' | Out-Host
    }
    $list = (& wsl.exe -l -q 2>$null) -replace "`0", ''
    if (-not ($list -match [regex]::Escape($script:Distro))) {
        Write-Host "Installing WSL2 distribution $script:Distro (reboot if Windows asks, then run this again)"
        & wsl.exe --install -d $script:Distro
        return
    }
    Invoke-SetuWsl "printf '[boot]\nsystemd=true\n' > /etc/wsl.conf" | Out-Null
    Invoke-SetuWsl @'
set -e
apt-get update -q
apt-get install -y -q curl tcpdump tshark iperf3 jq ethtool iproute2 build-essential python3 git ansible
command -v containerlab >/dev/null || curl -sL https://containerlab.dev/setup | bash -s "all"
'@ | Out-Host
    Write-Host 'Done. Run: wsl --shutdown   then   Test-SetuHost'
}

function Test-SetuHost {
    <# Week 0. Checks the kernel features each week needs. #>
    [CmdletBinding()] param()
    $script = @'
for m in sch_netem sch_htb sch_fq_codel tun bonding 8021q openvswitch nf_conntrack mac802154_hwsim; do
  if modprobe $m 2>/dev/null || [ -d /sys/module/$m ]; then echo "$m=OK"; else echo "$m=MISSING"; fi
done
ip link add setuchk type dummy 2>/dev/null && ip link set setuchk up && tc qdisc add dev setuchk root netem delay 1ms 2>/dev/null && echo "netem-functional=OK" || echo "netem-functional=MISSING"
ip link del setuchk 2>/dev/null
[ -c /dev/net/tun ] && echo "dev-net-tun=OK" || echo "dev-net-tun=MISSING"
command -v docker >/dev/null && echo "docker=OK" || echo "docker=MISSING"
command -v containerlab >/dev/null && echo "containerlab=OK" || echo "containerlab=MISSING"
echo "cores=$(nproc)"; echo "memoryGB=$(awk '/MemTotal/{printf "%.1f",$2/1048576}' /proc/meminfo)"
'@
    $weeks = @{ 'sch_netem'='2,6,7,12'; 'netem-functional'='2,6,7,12'; 'sch_htb'='12'; 'sch_fq_codel'='12';
                'tun'='6,7'; 'dev-net-tun'='6,7'; 'bonding'='9'; '8021q'='9'; 'openvswitch'='9';
                'nf_conntrack'='8,11'; 'mac802154_hwsim'='10 (optional)'; 'docker'='all'; 'containerlab'='all' }
    foreach ($line in (Invoke-SetuWsl $script -AllowFailure)) {
        if ($line -match '^([^=]+)=(.*)$') {
            [pscustomobject]@{ Feature = $Matches[1]; Status = $Matches[2]; NeededInWeeks = $weeks[$Matches[1]] }
        }
    }
}

# ---------------------------------------------------------------- lab lifecycle
function Start-SetuLab {
    <# Deploys a topology file, e.g. Start-SetuLab .\link\topology\w01-skeleton.clab.yml #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Topology)
    $p = ConvertTo-SetuWslPath $Topology
    $name = (Select-String -Path $Topology -Pattern '^name:\s*(\S+)').Matches[0].Groups[1].Value
    $script:Lab = $name; $env:SETU_LAB = $name
    Invoke-SetuWsl "containerlab deploy -t '$p' --reconfigure" | Out-Host
    Get-SetuLab
}

function Stop-SetuLab {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Topology)
    Invoke-SetuWsl "containerlab destroy -t '$(ConvertTo-SetuWslPath $Topology)' --cleanup" | Out-Host
}

function Get-SetuLab {
    <# Lists running lab devices with their management addresses. #>
    $json = Invoke-SetuWsl "containerlab inspect --all --format json" -AllowFailure
    try { $data = ($json -join '') | ConvertFrom-Json } catch { return $json }
    $rows = if ($data.PSObject.Properties.Name -contains 'containers') { $data.containers } else { $data.PSObject.Properties.Value | ForEach-Object { $_ } }
    $rows | Select-Object @{n='Device';e={$_.name}}, @{n='Image';e={$_.image}}, @{n='State';e={$_.state}}, @{n='MgmtIPv4';e={$_.ipv4_address}}
}

# ---------------------------------------------------------------- device configuration
function New-SetuBridge {
    <# Turns a Linux node into a Layer-2 switch: one bridge, listed ports as members. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string[]]$Ports, [switch]$VlanFiltering, [switch]$Stp)
    $vf = if ($VlanFiltering) { 1 } else { 0 }; $st = if ($Stp) { 1 } else { 0 }
    $cmd = "ip link add br0 type bridge vlan_filtering $vf stp_state $st 2>/dev/null; ip link set br0 up"
    foreach ($p in $Ports) { $cmd += "; ip link set $p master br0; ip link set $p up" }
    Invoke-SetuNode -Node $Node -Command $cmd | Out-Null
    Invoke-SetuNode -Node $Node -Command 'bridge link'
}

function Set-SetuAddress {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Interface,
        [Parameter(Mandatory)][string]$Address, [string]$Gateway)
    $cmd = "ip addr replace $Address dev $Interface; ip link set $Interface up"
    if ($Gateway) { $cmd += "; ip route replace default via $Gateway" }
    Invoke-SetuNode -Node $Node -Command $cmd | Out-Null
    Invoke-SetuNode -Node $Node -Command "ip -br addr show $Interface"
}

function Enable-SetuRouting {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node)
    Invoke-SetuNode -Node $Node -Command 'sysctl -w net.ipv4.ip_forward=1; sysctl -w net.ipv6.conf.all.forwarding=1'
}

function Disable-SetuOffload {
    [CmdletBinding()] param([Parameter(Mandatory)][string[]]$Node, [string]$Interface = 'eth1')
    foreach ($n in $Node) {
        Invoke-SetuNode -Node $n -Command "ethtool -K $Interface tso off gso off gro off tx off >/dev/null 2>&1; ethtool -k $Interface | grep -E 'tcp-segmentation-offload|tx-checksumming'"
    }
}

function Set-SetuLink {
    <# Takes a device interface down or up (link-failure tests). #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Interface,
        [Parameter(Mandatory)][ValidateSet('up','down')][string]$State)
    Invoke-SetuNode -Node $Node -Command "ip link set $Interface $State"
}

function Copy-SetuFile {
    <# Copies a file from Windows into a device, e.g. a router config. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Destination)
    $src = ConvertTo-SetuWslPath $Path
    Invoke-SetuWsl "docker cp '$src' $(Get-SetuContainer $Node):'$Destination'" | Out-Null
}

function Invoke-SetuFrr {
    <# Runs FRRouting CLI commands on a router: Invoke-SetuFrr rtr 'show ip ospf neighbor' #>
    [CmdletBinding()] param([Parameter(Mandatory, Position=0)][string]$Node, [Parameter(Mandatory, Position=1)][string[]]$Command)
    $vtyArgs = ($Command | ForEach-Object { "-c '$($_ -replace "'", '')'" }) -join ' '
    Invoke-SetuNode -Node $Node -Command "vtysh $vtyArgs"
}

function Set-SetuFrrConfig {
    <# Loads a Windows-side FRR config file into a router and applies it. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Path)
    Copy-SetuFile -Node $Node -Path $Path -Destination '/tmp/setu-frr.conf'
    Invoke-SetuNode -Node $Node -Command 'vtysh -f /tmp/setu-frr.conf && vtysh -c "write memory"'
}


# ---------------------------------------------------------------- switching (week 9, Linux bridge on each switch node)
function Set-SetuVlanPort {
    <# Access port: -AccessVlan 20.  Trunk port: -TrunkVlans 10,20,30,40 (all tagged, nothing untagged). #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Port,
        [int]$AccessVlan, [int[]]$TrunkVlans)
    $cmd = "bridge vlan del dev $Port vid 1 2>/dev/null; "
    if ($AccessVlan) { $cmd += "bridge vlan add dev $Port vid $AccessVlan pvid untagged; " }
    foreach ($v in $TrunkVlans) { $cmd += "bridge vlan add dev $Port vid $v; " }
    $cmd += "bridge vlan show dev $Port"
    Invoke-SetuNode -Node $Node -Command $cmd
}

function Set-SetuStpPriority {
    <# Lower priority wins the root election (0-65535, steps of 4096 by convention). #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][int]$Priority)
    Invoke-SetuNode -Node $Node -Command "ip link set br0 type bridge priority $Priority; cat /sys/class/net/br0/bridge/root_id"
}

function Get-SetuStp {
    <# Shows this switch's view of the root bridge and the state of every port (forwarding / blocking). #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node)
    $root = (Invoke-SetuNode -Node $Node -Command 'cat /sys/class/net/br0/bridge/root_id') -join ''
    $me = (Invoke-SetuNode -Node $Node -Command 'cat /sys/class/net/br0/bridge/bridge_id') -join ''
    $ports = Invoke-SetuNode -Node $Node -Command 'for p in /sys/class/net/br0/brif/*; do n=$(basename $p); s=$(cat $p/state); c=$(cat $p/path_cost); echo "$n $s $c"; done'
    $names = @{ '0'='disabled'; '1'='listening'; '2'='learning'; '3'='forwarding'; '4'='blocking' }
    foreach ($l in $ports) {
        $f = $l -split ' '
        [pscustomobject]@{ Switch = $Node; IsRoot = ($root -eq $me); RootBridge = $root; Port = $f[0]; State = $names[$f[1]]; PathCost = [int]$f[2] }
    }
}

function New-SetuBond {
    <# LACP (802.3ad) bond from member ports, hashed on IP addresses and ports (layer3+4). #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [string]$Name = 'bond0', [Parameter(Mandatory)][string[]]$Members,
        [ValidateSet('layer2','layer3+4')][string]$Hash = 'layer3+4')
    $cmd = "ip link add $Name type bond mode 802.3ad xmit_hash_policy $Hash miimon 100; "
    foreach ($m in $Members) { $cmd += "ip link set $m down; ip link set $m master $Name; " }
    $cmd += "ip link set $Name up; cat /proc/net/bonding/$Name | head -20"
    Invoke-SetuNode -Node $Node -Command $cmd
}

# ---------------------------------------------------------------- traffic and measurement
function Send-SetuUdp {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$From, [Parameter(Mandatory)][string]$To,
        [int]$Port = 5683, [string]$Text = 'temp=23.4C id=041')
    $safe = $Text -replace "'", ''
    Invoke-SetuNode -Node $From -Command "printf '%s' '$safe' | nc -u -w1 $To $Port" -AllowFailure | Out-Null
    "sent $($safe.Length) bytes UDP $From -> ${To}:$Port"
}

function Start-SetuIperfServer {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node)
    Invoke-SetuNode -Node $Node -Command 'pkill iperf3 2>/dev/null; iperf3 -s -D && echo iperf3-server-started'
}

function Test-SetuPing {
    <# 1000 probes by default; returns percentiles (never just the mean). #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$From, [Parameter(Mandatory)][string]$To,
        [int]$Count = 1000, [double]$IntervalSec = 0.01)
    $raw = Invoke-SetuNode -Node $From -Command "ping -c $Count -i $IntervalSec $To" -AllowFailure
    $rtts = foreach ($l in $raw) { if ($l -match 'time=([0-9.]+)') { [double]$Matches[1] } }
    $rx = @($rtts).Count
    [pscustomobject]@{
        From = $From; To = $To; Sent = $Count; Received = $rx
        LossPct = [Math]::Round(100.0 * ($Count - $rx) / $Count, 2)
        P50ms = Get-SetuPercentile $rtts 50; P95ms = Get-SetuPercentile $rtts 95
        P99ms = Get-SetuPercentile $rtts 99
        MaxMs = if ($rx) { ($rtts | Measure-Object -Maximum).Maximum } else { $null }
        MeanMs = if ($rx) { [Math]::Round(($rtts | Measure-Object -Average).Average, 3) } else { $null }
    }
}

function Measure-SetuThroughput {
    <# iperf3 runs with warm-up discarded; returns each run and the median. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$From, [Parameter(Mandatory)][string]$To,
        [int]$Seconds = 30, [int]$OmitSeconds = 5, [int]$Runs = 3, [string]$Congestion, [switch]$Udp, [string]$UdpRate = '100M')
    $results = for ($i = 1; $i -le $Runs; $i++) {
        $extra = ''
        if ($Congestion) { $extra += " -C $Congestion" }
        if ($Udp) { $extra += " -u -b $UdpRate" }
        $json = Invoke-SetuNode -Node $From -Command "iperf3 -c $To -t $Seconds -O $OmitSeconds -J$extra"
        $o = ($json -join '') | ConvertFrom-Json
        $bps = if ($Udp) { $o.end.sum.bits_per_second } else { $o.end.sum_received.bits_per_second }
        [Math]::Round($bps / 1e6, 1)
    }
    [pscustomobject]@{ RunsMbit = $results; MedianMbit = Get-SetuPercentile $results 50
        MinMbit = ($results | Measure-Object -Minimum).Minimum; MaxMbit = ($results | Measure-Object -Maximum).Maximum }
}

function Set-SetuImpairment {
    <# Adds delay, jitter, loss, reordering, rate limit or buffer size on a device's egress. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Interface,
        [double]$DelayMs, [double]$JitterMs, [double]$LossPct, [double]$LossCorrelationPct,
        [double]$RateMbit, [int]$LimitPackets, [double]$ReorderPct)
    $netem = New-SetuNetemArgs -DelayMs $DelayMs -JitterMs $JitterMs -LossPct $LossPct -LossCorrelationPct $LossCorrelationPct -RateMbit $RateMbit -LimitPackets $LimitPackets -ReorderPct $ReorderPct
    if (-not $netem) { throw 'Give at least one impairment, e.g. -DelayMs 20' }
    Invoke-SetuNode -Node $Node -Command "tc qdisc replace dev $Interface root netem $netem" | Out-Null
    Get-SetuImpairment -Node $Node -Interface $Interface
}

function Clear-SetuImpairment {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Interface)
    Invoke-SetuNode -Node $Node -Command "tc qdisc del dev $Interface root 2>/dev/null; true" | Out-Null
    Get-SetuImpairment -Node $Node -Interface $Interface
}

function Get-SetuImpairment {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Interface)
    Invoke-SetuNode -Node $Node -Command "tc -s qdisc show dev $Interface"
}

function Invoke-SetuCapture {
    <# Captures on several taps at once while a script block generates traffic; pcaps land in C:\setu\captures.
       Invoke-SetuCapture -Taps 'dev01:eth1','rtr:eth1','srv01:eth1' -Name w01 -Seconds 6 -During { Send-SetuUdp dev01 10.20.8.12 } #>
    [CmdletBinding()] param([Parameter(Mandatory)][string[]]$Taps, [string]$Name = 'cap',
        [int]$Seconds = 8, [scriptblock]$During, [string]$Filter = '', [switch]$Open)
    $dir = Join-Path $script:Root 'captures'; New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $files = @(); $bash = ''
    $i = 1
    foreach ($t in $Taps) {
        $node, $if = $t.Split(':')
        $file = Join-Path $dir ("{0}-tap{1}-{2}.pcap" -f $Name, $i, $node)
        $files += $file
        $bash += "timeout $Seconds ip netns exec $(Get-SetuContainer $node) tcpdump -U -n -i $if -w '$(ConvertTo-SetuWslPath $file)' $Filter >/dev/null 2>&1 & "
        $i++
    }
    $bash += 'wait'
    $b64 = ConvertTo-SetuB64 $bash
    $proc = Start-Process -FilePath 'wsl.exe' -ArgumentList @('-d', $script:Distro, '-u', 'root', '--', 'bash', '-c', "echo $b64 | base64 -d | bash") -NoNewWindow -PassThru
    Start-Sleep -Seconds 1
    if ($During) { & $During | Out-Host }
    $proc.WaitForExit()
    $files | ForEach-Object { Get-Item $_ -ErrorAction SilentlyContinue | Select-Object Name, Length, FullName }
    if ($Open) { $files | ForEach-Object { Open-SetuCapture $_ } }
}

function Open-SetuCapture {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Path)
    $ws = @("$env:ProgramFiles\Wireshark\Wireshark.exe", "${env:ProgramFiles(x86)}\Wireshark\Wireshark.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $ws) { throw 'Wireshark not found. Run Install-SetuPrerequisites.' }
    Start-Process -FilePath $ws -ArgumentList "`"$Path`""
}

function Measure-SetuConvergence {
    <# Pings every 100 ms, fails one or more interfaces after 5 s, restores them at the end. Convergence = lost probes x 0.1 s. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$From, [Parameter(Mandatory)][string]$To,
        [Parameter(Mandatory)][string]$FailNode, [Parameter(Mandatory)][string[]]$FailInterface, [int]$Probes = 200)
    $c = Get-SetuContainer $From; $f = Get-SetuContainer $FailNode
    $down = ($FailInterface | ForEach-Object { "docker exec $f ip link set $_ down" }) -join '; '
    $up   = ($FailInterface | ForEach-Object { "docker exec $f ip link set $_ up" }) -join '; '
    $out = Invoke-SetuWsl @"
docker exec $c ping -i 0.1 -c $Probes $To > /tmp/setu-conv.txt 2>&1 &
sleep 5; $down
wait; $up
grep -E 'packets transmitted' /tmp/setu-conv.txt
"@ -AllowFailure
    $line = ($out | Select-String 'packets transmitted').Line
    if ($line -match '(\d+) packets transmitted, (\d+) (packets )?received') {
        $lost = [int]$Matches[1] - [int]$Matches[2]
        [pscustomobject]@{ ProbesSent = [int]$Matches[1]; Lost = $lost; ConvergenceSec = [Math]::Round($lost * 0.1, 1) }
    } else { $out }
}

function New-SetuBaseline {
    <# Week 2 deliverable. Writes link\verify\BASELINE-<time>.md with conditions, RTT percentiles, throughput and error counters. #>
    [CmdletBinding()] param([string]$From = 'dev01', [string]$ToNode = 'srv01', [Parameter(Mandatory)][string]$ToAddress,
        [string]$Interface = 'eth1', [int]$Pings = 1000, [int]$Runs = 3)
    $file = Join-Path $script:Root ("link\verify\BASELINE-{0}.md" -f (Get-Date -Format 'yyyyMMdd-HHmm'))
    $load = (Invoke-SetuWsl 'cat /proc/loadavg') -join ' '
    $cores = (Invoke-SetuWsl 'nproc') -join ''
    $off = (Invoke-SetuNode -Node $From -Command "ethtool -k $Interface | grep -E 'tcp-segmentation|generic-segmentation'" -AllowFailure) -join '; '
    $qScript = @'
for c in $(docker ps --format '{{.Names}}' | grep "^clab-LABNAME-"); do docker exec $c tc qdisc show 2>/dev/null | grep netem | sed "s/^/$c: /"; done
'@
    $q = (Invoke-SetuWsl ($qScript -replace 'LABNAME', $script:Lab) -AllowFailure) -join '; '
    if (-not $q) { $q = 'none' }
    Start-SetuIperfServer -Node $ToNode | Out-Null
    $rtt = Test-SetuPing -From $From -To $ToAddress -Count $Pings
    $tp = Measure-SetuThroughput -From $From -To $ToAddress -Runs $Runs
    $errs = foreach ($n in $From, $ToNode) { "### $n"; Invoke-SetuNode -Node $n -Command "ip -s link show $Interface | tail -4" }
    @"
# SETU baseline
generated: $(Get-Date -Format o)

## Conditions
- engine load: $load
- engine cores: $cores
- offloads on ${From}: $off
- netem qdiscs installed: $q

## RTT distribution - $Pings probes, $From -> $ToAddress
p50 $($rtt.P50ms) ms | p95 $($rtt.P95ms) ms | p99 $($rtt.P99ms) ms | max $($rtt.MaxMs) ms | loss $($rtt.LossPct) %

## Throughput - $Runs runs, 30 s, first 5 s discarded
runs: $($tp.RunsMbit -join ', ') Mbit/s | median $($tp.MedianMbit) Mbit/s | range $($tp.MinMbit)-$($tp.MaxMbit)

## Interface counters (errors must be zero at baseline)
$($errs -join [Environment]::NewLine)
"@ | Set-Content -Path $file -Encoding utf8
    Write-Host "Baseline written: $file"
    [pscustomobject]@{ File = $file; Rtt = $rtt; Throughput = $tp; Qdiscs = $q }
}

# ---------------------------------------------------------------- services
function Invoke-SetuDhcpLease {
    <# Flushes the interface and requests a lease (DORA) from the device. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [string]$Interface = 'eth1')
    Invoke-SetuNode -Node $Node -Command "ip addr flush dev $Interface; udhcpc -i $Interface -n -q -t 4 2>&1; ip -br addr show $Interface" -AllowFailure
}

function Start-SetuDhcpRelay {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$ClientInterface,
        [Parameter(Mandatory)][string]$UpstreamInterface, [Parameter(Mandatory)][string]$Server)
    Invoke-SetuNode -Node $Node -Command "command -v dhcrelay >/dev/null || apk add --no-cache dhcrelay >/dev/null 2>&1 || (apt-get update -q && apt-get install -y -q isc-dhcp-relay) >/dev/null 2>&1; pkill dhcrelay 2>/dev/null; dhcrelay -4 -id $ClientInterface -iu $UpstreamInterface $Server && echo relay-started"
}

function Stop-SetuDhcpRelay {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node)
    Invoke-SetuNode -Node $Node -Command 'pkill dhcrelay; echo relay-stopped' -AllowFailure
}

# ---------------------------------------------------------------- QoS (week 12)
function Set-SetuDscpEdge {
    <# Remarks everything entering on an interface to DSCP 0, then marks trusted telemetry (UDP port) as EF. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Interface, [int]$TelemetryUdpPort = 5683)
    Invoke-SetuNode -Node $Node -Command @"
iptables -t mangle -F PREROUTING
iptables -t mangle -A PREROUTING -i $Interface -j DSCP --set-dscp 0
iptables -t mangle -A PREROUTING -i $Interface -p udp --dport $TelemetryUdpPort -j DSCP --set-dscp-class EF
iptables -t mangle -L PREROUTING -v -n
"@
}

function Set-SetuQos {
    <# HTB link shaper with a capped EF class, AF41, AF31 and a default class under FQ-CoDel. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Interface,
        [int]$LinkMbit = 1000, [int]$EfMbit = 300, [int]$Af41Pct = 25, [int]$Af31Pct = 5, [switch]$NoAqm)
    $af41 = [int]($LinkMbit * $Af41Pct / 100); $af31 = [int]($LinkMbit * $Af31Pct / 100)
    $df = [Math]::Max(1, $LinkMbit - $EfMbit - $af41 - $af31)
    $aqm = if ($NoAqm) { "tc qdisc add dev $Interface parent 1:30 pfifo limit 1000" } else { "tc qdisc add dev $Interface parent 1:30 fq_codel target 5ms interval 100ms" }
    Invoke-SetuNode -Node $Node -Command @"
tc qdisc del dev $Interface root 2>/dev/null
tc qdisc add dev $Interface root handle 1: htb default 30
tc class add dev $Interface parent 1:  classid 1:1  htb rate ${LinkMbit}mbit
tc class add dev $Interface parent 1:1 classid 1:10 htb rate ${EfMbit}mbit ceil ${EfMbit}mbit prio 0
tc class add dev $Interface parent 1:1 classid 1:20 htb rate ${af41}mbit ceil ${LinkMbit}mbit prio 1
tc class add dev $Interface parent 1:1 classid 1:25 htb rate ${af31}mbit ceil ${LinkMbit}mbit prio 1
tc class add dev $Interface parent 1:1 classid 1:30 htb rate ${df}mbit ceil ${LinkMbit}mbit prio 2
$aqm
tc filter add dev $Interface parent 1: protocol ip u32 match ip dsfield 0xb8 0xfc flowid 1:10
tc filter add dev $Interface parent 1: protocol ip u32 match ip dsfield 0x88 0xfc flowid 1:20
tc filter add dev $Interface parent 1: protocol ip u32 match ip dsfield 0x68 0xfc flowid 1:25
tc -s class show dev $Interface
"@
}

function Set-SetuShaper {
    <# Plain bottleneck with no QoS (control run): one HTB class at the link rate and a FIFO. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string]$Interface, [int]$LinkMbit = 1000)
    Invoke-SetuNode -Node $Node -Command "tc qdisc del dev $Interface root 2>/dev/null; tc qdisc add dev $Interface root handle 1: htb default 1; tc class add dev $Interface parent 1: classid 1:1 htb rate ${LinkMbit}mbit; tc qdisc add dev $Interface parent 1:1 pfifo limit 1000; tc -s qdisc show dev $Interface"
}

# ---------------------------------------------------------------- week 6 gate (runs your stack in the engine)
function Invoke-SetuInteropGate {
    <# Builds stack\ inside the engine and runs a 1 MB transfer between your TCP and the Linux kernel over a TUN device. #>
    [CmdletBinding()] param([ValidateSet('clean','delay','jitter','loss','reorder','smallmtu')][string]$Profile = 'clean',
        [ValidateSet('send','receive')][string]$Direction = 'send')
    $stack = ConvertTo-SetuWslPath (Join-Path $script:Root 'stack')
    $netem = switch ($Profile) {
        'clean' { '' } 'delay' { 'delay 50ms' } 'jitter' { 'delay 50ms 10ms distribution normal' }
        'loss' { 'loss 1% 25%' } 'reorder' { 'delay 50ms reorder 25% 50%' } 'smallmtu' { '' } }
    $mtu = if ($Profile -eq 'smallmtu') { 200 } else { 1500 }
    $tc = if ($netem) { "tc qdisc replace dev tun144 root netem $netem" } else { 'tc qdisc del dev tun144 root 2>/dev/null; true' }
    $run = if ($Direction -eq 'send') {
        "(timeout 120 nc -l 9090 > /tmp/recv.bin &) ; sleep 1; timeout 120 ./build-linux/setu_tcp connect 169.254.144.1 9090 < /tmp/send.bin; sleep 2"
    } else {
        "(timeout 120 ./build-linux/setu_tcp listen 9090 > /tmp/recv.bin &) ; sleep 1; timeout 120 nc -N 169.254.144.9 9090 < /tmp/send.bin; sleep 2"
    }
    Invoke-SetuWsl @"
set -e
cd '$stack'
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug >/dev/null && cmake --build build-linux -j >/dev/null
ip tuntap add dev tun144 mode tun 2>/dev/null || true
ip addr replace 169.254.144.1/24 dev tun144; ip link set tun144 mtu $mtu up
$tc
head -c 1048576 /dev/urandom > /tmp/send.bin
$run
a=`$(sha256sum /tmp/send.bin | cut -c1-64); b=`$(sha256sum /tmp/recv.bin | cut -c1-64)
echo "profile=$Profile direction=$Direction sent=`$a received=`$b"
[ "`$a" = "`$b" ] && echo GATE-PASS || echo GATE-FAIL
"@ -AllowFailure
}

# ---------------------------------------------------------------- automation (week 11)
function Invoke-SetuAnsible {
    <# Runs an Ansible playbook from the engine (Ansible has no native Windows control node). #>
    [CmdletBinding()] param([string]$Playbook = 'link\ansible\site.yml', [string]$Inventory = 'link\ansible\inventory.yml', [switch]$Check)
    $pb = ConvertTo-SetuWslPath (Join-Path $script:Root $Playbook)
    $inv = ConvertTo-SetuWslPath (Join-Path $script:Root $Inventory)
    $chk = if ($Check) { '--check --diff' } else { '' }
    Invoke-SetuWsl "cd /tmp && ANSIBLE_HOST_KEY_CHECKING=False ansible-playbook -i '$inv' '$pb' $chk" -AllowFailure
}

function Get-SetuConntrack {
    <# State-table occupancy on a router: L = lambda x W in practice. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node)
    $o = Invoke-SetuNode -Node $Node -Command 'cat /proc/sys/net/netfilter/nf_conntrack_count /proc/sys/net/netfilter/nf_conntrack_max' -AllowFailure
    if (@($o).Count -ge 2) { [pscustomobject]@{ Entries = [int]$o[0]; Max = [int]$o[1]; Utilisation = [Math]::Round([int]$o[0] / [int]$o[1], 4) } } else { $o }
}

# ---------------------------------------------------------------- Windows-native wireless survey (week 10)
function Get-SetuWifiSurvey {
    <# Uses this laptop's own Wi-Fi adapter (netsh) to list access points per channel. English Windows output. #>
    [CmdletBinding()] param()
    $lines = netsh wlan show networks mode=bssid
    $ssid = ''; $cur = $null
    $rows = foreach ($l in $lines) {
        if ($l -match '^SSID \d+ : (.*)$') { $ssid = $Matches[1].Trim() }
        elseif ($l -match '^\s+BSSID \d+\s+: (.*)$') { if ($cur) { $cur }; $cur = [ordered]@{ SSID = $ssid; BSSID = $Matches[1].Trim(); SignalPct = $null; Channel = $null; Band = $null } }
        elseif ($cur -and $l -match '^\s+Signal\s+: (\d+)%') { $cur.SignalPct = [int]$Matches[1] }
        elseif ($cur -and $l -match '^\s+Band\s+: (.*)$') { $cur.Band = $Matches[1].Trim() }
        elseif ($cur -and $l -match '^\s+Channel\s+: (\d+)') { $cur.Channel = [int]$Matches[1] }
    }
    if ($cur) { $rows = @($rows) + $cur }
    $objs = $rows | ForEach-Object { [pscustomobject]$_ }
    $objs | Sort-Object @{e='Channel'}, @{e='SignalPct'; Descending=$true}
}

# ---------------------------------------------------------------- week 5: concurrency model 4 (epoll) in the engine
function Start-SetuEpollServer {
    <# Builds stack\src\server_epoll.c inside the lab engine and starts it in the background (log: /tmp/setu-epoll.log). #>
    [CmdletBinding()] param([ValidateSet('et','lt')][string]$Mode = 'et', [int]$Port = 8084)
    $stack = ConvertTo-SetuWslPath (Join-Path $script:Root 'stack')
    Invoke-SetuWsl @"
set -e
pkill -f 'server_epoll ' 2>/dev/null || true
cd '$stack'
gcc -std=c17 -O2 -Wall -Wextra -Werror -o /tmp/server_epoll src/server_epoll.c
ulimit -n 65536
nohup /tmp/server_epoll $Mode $Port > /tmp/setu-epoll.log 2>&1 &
sleep 1; head -2 /tmp/setu-epoll.log
"@
}
function Stop-SetuEpollServer { [CmdletBinding()] param() Invoke-SetuWsl "pkill -f 'server_epoll ' ; tail -3 /tmp/setu-epoll.log" -AllowFailure }

function Invoke-SetuC10kEngine {
    <# Runs tools\c10k_client.py inside the engine against a server in the engine (no Windows port relay in the path). #>
    [CmdletBinding()] param([int]$Port = 8084, [int]$Count = 10000, [int]$HoldSeconds = 60)
    $tools = ConvertTo-SetuWslPath (Join-Path $script:Root 'tools')
    Invoke-SetuWsl "ulimit -n 65536; python3 '$tools/c10k_client.py' --port $Port --count $Count --hold $HoldSeconds" -AllowFailure
}

function Get-SetuAcceptQueue {
    <# The SLM's accept-queue metric: for a LISTENING socket, ss shows Recv-Q = connections waiting for accept()
       and Send-Q = the backlog limit. Samples every 0.2 s and adds the kernel's overflow counters. #>
    [CmdletBinding()] param([int]$Port = 8084, [int]$Samples = 25)
    Invoke-SetuWsl @"
for i in `$(seq $Samples); do ss -lnt "sport = :$Port" | awk 'NR==2 {print "queued=" `$2 "  backlog=" `$3}'; sleep 0.2; done
nstat -az TcpExtListenOverflows TcpExtListenDrops 2>/dev/null | tail -2
"@ -AllowFailure
}

# ---------------------------------------------------------------- week 8, part 2: the edge lab
function Get-SetuVrrp {
    <# Which router holds the BLD-C virtual gateway 10.20.36.1 right now. #>
    [CmdletBinding()] param()
    'r-c', 'r-c2' | ForEach-Object {
        $has = (Invoke-SetuNode -Node $_ -Command 'ip -4 -o addr show dev eth3' -AllowFailure) -match '10\.20\.36\.1/'
        [pscustomobject]@{ Router = $_; Role = if ($has) { 'MASTER (holds 10.20.36.1)' } else { 'backup' } }
    }
}

# ---------------------------------------------------------------- week 9: RSTP on Open vSwitch
function Enable-SetuOvsRstp {
    <# Replaces a node's Linux bridge with an Open vSwitch bridge running RSTP (802.1w).
       -AccessVlan @{ eth3 = 10 } makes eth3 an access port; untagged-config ports are trunks. Priority: multiple of 4096. #>
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node, [Parameter(Mandatory)][string[]]$Ports,
        [hashtable]$AccessVlan = @{}, [int]$Priority = 32768)
    $lines = @(
        'ip link del br0 2>/dev/null; ip link del bond0 2>/dev/null; true',
        'command -v ovs-vsctl >/dev/null || apk add --no-cache openvswitch >/dev/null',
        'mkdir -p /etc/openvswitch /var/run/openvswitch',
        '[ -f /etc/openvswitch/conf.db ] || ovsdb-tool create /etc/openvswitch/conf.db /usr/share/openvswitch/vswitch.ovsschema',
        'pgrep ovsdb-server >/dev/null || ovsdb-server --remote=punix:/var/run/openvswitch/db.sock --pidfile --detach',
        'ovs-vsctl --no-wait init',
        'pgrep ovs-vswitchd >/dev/null || ovs-vswitchd --pidfile --detach',
        'ovs-vsctl --may-exist add-br ovsbr0',
        "ovs-vsctl set Bridge ovsbr0 rstp_enable=true other_config:rstp-priority=$Priority")
    foreach ($p in $Ports) {
        $tag = if ($AccessVlan.ContainsKey($p)) { " tag=$($AccessVlan[$p])" } else { '' }
        $lines += "ip link set $p nomaster 2>/dev/null; ip link set $p up; ovs-vsctl --may-exist add-port ovsbr0 $p$tag"
    }
    $lines += 'ip link set ovsbr0 up', 'ovs-vsctl show | head -20'
    Invoke-SetuNode -Node $Node -Command ($lines -join "`n")
}
function Get-SetuRstp {
    [CmdletBinding()] param([Parameter(Mandatory)][string]$Node)
    Invoke-SetuNode -Node $Node -Command 'ovs-appctl rstp/show ovsbr0'
}

Export-ModuleMember -Function *-Setu*, Invoke-SetuWsl, ConvertTo-SetuWslPath
