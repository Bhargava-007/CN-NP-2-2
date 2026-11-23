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

## Final cleanup
- [ ] `wsl --unregister setu-lab ; Remove-Item -Recurse $env:LOCALAPPDATA\setu-lab`
- [ ] `[Environment]::SetEnvironmentVariable('SETU_DISTRO',$null,'User')`
- [ ] `Remove-Item $HOME\.wslconfig ; wsl --shutdown`
- [ ] `Remove-Item $env:TEMP\wsl-*.txt`
- [ ] `git rm -r CapStone-Project/setu` (or delete the `setu` folder)

Pre-existing distro "Ubuntu" is left alone.
