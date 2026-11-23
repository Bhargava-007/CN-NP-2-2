# SETU Windows Lab Kit - CNNP Network Studio (25CS2208E)

Unzip to `C:\setu`. Everything is driven from **PowerShell 7** on Windows 10/11.

```powershell
# once, in an Administrator PowerShell
winget install Microsoft.PowerShell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
Import-Module C:\setu\SetuLab\SetuLab.psm1
Install-SetuPrerequisites          # Windows apps + WSL2 Ubuntu + Docker + containerlab (reboot if asked, run again)
wsl --shutdown
Test-SetuHost                      # which weeks this laptop can run
```

## What runs where

| Runs natively on Windows | Runs in the lab engine (WSL2), driven by SetuLab cmdlets |
| --- | --- |
| Your C code: webget, HTTP server, DNS resolver, concurrency models 1-3 (threads, select, WSAPoll) plus IOCP as an optional comparison, TCP building blocks, LPM router, CRC-32 - built with Visual Studio 2022 Build Tools, debugged in VS Code | The virtual campus: routers (FRRouting), switches, hosts, DHCP (Kea), DNS (BIND), impairment (delay/loss/jitter), QoS queues, packet capture taps, Ansible |
| Wireshark, Edge/Chrome, nslookup, Python tools, plots, Wi-Fi survey (`Get-SetuWifiSurvey`) | The Interop Gate harness for your TCP (`Invoke-SetuInteropGate`), because the SLM requires a Linux kernel TCP peer; concurrency model 4 (epoll, `server_epoll.c`), because epoll is Linux-only |

The devices in the lab are network appliances. When a step configures a device, the cmdlet sends the
device its own CLI (FRR, `ip`, `tc`) - the same way you would type IOS commands into a Cisco router from
a Windows PC. You never open a Linux shell.

## Folders

- `SetuLab\SetuLab.psm1` - every lab action as a cmdlet (`Get-Command -Module SetuLab`)
- `link\topology\` - week 1, 3, 8 (core and edge), 9, 12 topologies; `link\config\` - Kea, BIND, FRR configs; `link\ansible\`
- `stack\` - open in VS Code; `Ctrl+Shift+B` builds, `F5` debugs (configs in `stack\.vscode`)
- `tools\` - percentiles, Mathis plot, C10K client, HTTP probe, latency probe, fragmentation probe
- `captures\` - every capture lands here and opens in Wireshark

## Reference solutions are included

The `stack\src` files for every checkpoint (CP0-CP7) and `server_epoll.c` are complete, working
reference implementations, and every test passes out of the box. Use them the way you would use a
worked solution: attempt each checkpoint in your own file first, and open the reference when you are
stuck or want to compare your design. You will be asked to defend your own code, not this one.

## Verified before shipping

- All C sources compile with `-Wall -Wextra -Werror` for Windows (MinGW-w64) and Linux; unit tests pass
  (ByteStream 1 MB through capacity 2; unwrap/RTO/Reno; LPM with /0 and /32 plus TTL/checksum; CRC-32 check value,
  all single-bit errors, bursts up to 32 bits). The DNS resolver was checked against six record types.
- `server_epoll.c` held 10,000 idle connections and drained a 3,000-byte burst in edge-triggered mode.
- `server_iocp.c` is compile-checked for Windows only; run it on your laptop to observe it.
- `setu_tcp.c` is the gate harness: it opens the TUN device and stops at a clearly marked TODO, because the
  TCP connection itself is your CP3/CP4 work.
- The week 8 edge-lab router configurations pass FRRouting's syntax check; run `Test-SetuHost` on each laptop.
