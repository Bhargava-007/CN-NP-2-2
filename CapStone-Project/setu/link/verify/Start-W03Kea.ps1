Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$c = 'clab-setu-w03-kea01'

# 1. Fail with a clear message if container clab-setu-w03-kea01 is not running.
$running = Invoke-SetuWsl -Command 'docker inspect -f "{{.State.Running}}" clab-setu-w03-kea01 2>/dev/null || echo false' -AllowFailure
if (($running -join '').Trim() -ne 'true') {
    throw "Container $c is not running. Deploy w03-services.clab.yml first."
}

# 2. Install kea-dhcp4-server and iproute2 if kea-dhcp4 is missing.
Invoke-SetuWsl -Command 'docker exec clab-setu-w03-kea01 sh -c "command -v kea-dhcp4 >/dev/null 2>&1 || (apt-get update -q && DEBIAN_FRONTEND=noninteractive apt-get install -y -q -o Dpkg::Options::=--force-confold kea-dhcp4-server iproute2)"' | Out-Null

# 3. Configure eth1 IP, default route, and /run/kea.
Invoke-SetuWsl -Command 'docker exec clab-setu-w03-kea01 sh -c "ip -4 addr show dev eth1 | grep -q 10.20.48.9/25 || ip addr add 10.20.48.9/25 dev eth1; ip route replace default via 10.20.48.1; mkdir -p /run/kea"' | Out-Null

# 4. Kill any running kea-dhcp4 using in-container PIDs, then start detached.
Invoke-SetuWsl -Command 'docker exec clab-setu-w03-kea01 sh -c "for p in \$(ps -eo pid,comm | awk '\''\$2==\"kea-dhcp4\"{print \$1}'\''); do kill \$p 2>/dev/null || true; done"' -AllowFailure | Out-Null
Invoke-SetuWsl -Command 'docker exec -d clab-setu-w03-kea01 sh -c "exec kea-dhcp4 -c /etc/kea/kea-dhcp4.conf > /tmp/kea.log 2>&1"' | Out-Null
Start-Sleep -Seconds 2
$lastLine = Invoke-SetuWsl -Command 'docker exec clab-setu-w03-kea01 tail -n 1 /tmp/kea.log' -AllowFailure
Write-Output ("kea01 last log line: " + ($lastLine -join ' '))

# 5. Bring up bridges on sw-c and sw-dc, and delete default route via eth0 on dev01.
New-SetuBridge -Node 'sw-c' -Interfaces @('eth1', 'eth2') | Out-Null
New-SetuBridge -Node 'sw-dc' -Interfaces @('eth1', 'eth2', 'eth3', 'eth4') | Out-Null
Invoke-SetuWsl -Command 'docker exec clab-setu-w03-dev01 ip route del default dev eth0 2>/dev/null || true' -AllowFailure | Out-Null

# 6. Summary line: DHCP4_STARTED found or not.
$started = Invoke-SetuWsl -Command 'docker exec clab-setu-w03-kea01 grep -q DHCP4_STARTED /tmp/kea.log && echo FOUND || echo NOT_FOUND' -AllowFailure
Write-Output ("SUMMARY: DHCP4_STARTED " + (($started -join '').Trim()))
