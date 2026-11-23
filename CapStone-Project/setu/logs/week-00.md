# Week 0 log: setup (2026-11-23)
- WSL: hung during a cancelled wsl --install (service wedged); recovered by restarting WSLService. .wslconfig first used wrong keys (autoMemoryReclaim and sparseVhd belong under [experimental]); fixed.
- Distro: cloned existing Ubuntu (26.04.1) to setu-lab with wsl --export/--import (about 3 GB, tar deleted); sparse enabled on retry. SETU_DISTRO=setu-lab.
- Repo: capstone lives inside the coursework repo (root CN-NP-2-2, subfolder CapStone-Project\setu). The IDE sandbox cannot run git, so the student commits by path. Unrelated deletions in the working tree (Cisco and Skill PDFs, Coursera/.gitkeep) are never staged.
- Kit: 58 files; module imports on Windows PowerShell 5.1 (52 cmdlets), so PowerShell 7 was skipped.
- Installs inside setu-lab: tools via apt (about 1.3 GB), docker.io 29.1.3, containerlab 0.83.0 (unsigned apt repo, accepted risk).
- Pain points (mine, not the system): PowerShell 5.1 mangles quotes and pipes passed to wsl; use --exec and avoid inner quotes.
- Measured: Test-SetuHost all OK except mac802154_hwsim (optional, week 10); cores 8; memory 7.8 GB. Download speed inside setu-lab about 50 MB/s to Cloudflare; archive.ubuntu.com and netdevops.fury.site about 143 KB/s.
- Commits so far: chore(kit): import SETU lab kit as shipped; chore(repo): add gitignore for builds and captures; docs(ledger): add install ledger with before-snapshot.
