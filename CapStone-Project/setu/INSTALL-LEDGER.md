# INSTALL-LEDGER

## BEFORE snapshot (2026-11-23, before any change)
- Windows 10.0.26300; Windows PowerShell 5.1.26100 (pwsh 7 not installed and not needed: the kit module imports on 5.1, 52 cmdlets)
- git 2.56.0.windows.4, Python 3.13.16, winget 1.29.380
- WSL 3.0.5.0, kernel 6.18.58.1-microsoft-standard-WSL2
- One pre-existing distro "Ubuntu" (Ubuntu 26.04.1 LTS, vhdx 3.07 GB, user bhargav). It predates this project and must NOT be modified or deleted.
- Absent: pwsh, docker, cmake, gcc, cl, openssl on PATH, Wireshark, $HOME\.wslconfig, the SETU kit
- Execution policy CurrentUser was already RemoteSigned (not changed by this project)
- Repo: coursework repo root C:\Users\bharg\Downloads\College\CN-NP-2-2; capstone in CapStone-Project\setu

## Installed / changed by this project

| Item | Location | Removal command |
| :--- | :--- | :--- |
| SETU lab kit | CapStone-Project\setu (in the repo) | `git rm -r CapStone-Project/setu`, or delete the folder |
| WSL config | `$HOME\.wslconfig` (memory=8GB, processors=8, swap=2GB; experimental: autoMemoryReclaim=gradual, sparseVhd=true) | `Remove-Item $HOME\.wslconfig ; wsl --shutdown` |
| Distro setu-lab (clone of Ubuntu via wsl --export/--import, sparse) | `$env:LOCALAPPDATA\setu-lab\ext4.vhdx` | `wsl --unregister setu-lab ; Remove-Item -Recurse $env:LOCALAPPDATA\setu-lab` |
| User env var `SETU_DISTRO=setu-lab` | User environment | `[Environment]::SetEnvironmentVariable('SETU_DISTRO',$null,'User')` |
| Temp files | `$env:TEMP\wsl-*.txt` | `Remove-Item $env:TEMP\wsl-*.txt` |
| Cancelled `wsl --install -d Ubuntu-24.04` (no distro was registered; any cached download is unverified) | unknown | check `%TEMP%` and `%LOCALAPPDATA%\Packages` for leftovers at the end |
| Packages inside setu-lab: curl ca-certificates tcpdump tshark iperf3 jq ethtool iproute2 build-essential cmake netcat-openbsd python3 python3-venv git ansible docker.io dnsutils (dig 9.20.24) (about 1.3 GB added, measured: distro 2.0 GB -> 3.3 GB) | inside setu-lab only | removed by: `wsl --unregister setu-lab` |
| Docker 29.1.3 (Ubuntu docker.io package, enabled via systemd) | inside setu-lab | removed by: `wsl --unregister setu-lab` |
| containerlab 0.83.0 from apt source `/etc/apt/sources.list.d/netdevops.list` (`deb [trusted=yes] https://netdevops.fury.site/apt/ /`, unsigned repo, accepted risk, confined to setu-lab) | inside setu-lab | removed by: `wsl --unregister setu-lab` |
| `matplotlib` (Python venv) | `/tmp/pv` inside `setu-lab` only | removed when `/tmp` is cleared or `wsl --unregister setu-lab` |

## Final cleanup
- [ ] `wsl --unregister setu-lab ; Remove-Item -Recurse $env:LOCALAPPDATA\setu-lab`
- [ ] `[Environment]::SetEnvironmentVariable('SETU_DISTRO',$null,'User')`
- [ ] `Remove-Item $HOME\.wslconfig ; wsl --shutdown`
- [ ] `Remove-Item $env:TEMP\wsl-*.txt`
- [ ] `git rm -r CapStone-Project/setu` (or delete the `setu` folder)

Pre-existing distro "Ubuntu" is left alone.

## Week 0 verification (2026-11-23)
- `Test-SetuHost` result: `sch_netem`, `sch_htb`, `sch_fq_codel`, `tun`, `bonding`, `8021q`, `openvswitch`, `nf_conntrack`, `netem-functional`, `dev-net-tun`, `docker`, `containerlab` all OK; `mac802154_hwsim` MISSING (optional, week 10); cores 8; memoryGB 7.8.
- Tools verified inside `setu-lab`: `gcc` 15.2.0, `cmake` 4.2.3, `tshark` 4.6.4, `iperf3` 3.20, `ansible-core` 2.20.1, Docker 29.1.3 (cgroup v2, systemd driver).
- Download speed from inside `setu-lab` measured about 50 MB/s to Cloudflare; `archive.ubuntu.com` and `netdevops.fury.site` were slow (about 143 KB/s).
