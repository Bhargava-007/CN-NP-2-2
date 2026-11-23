# JOURNEY

## Week 0: setup (DRAFT, student to edit)
**Decision:** Run the lab in a dedicated WSL2 distro "setu-lab" (a local clone of my existing Ubuntu 26.04), with Docker (docker.io) and containerlab inside it, driven by the kit's SetuLab module from Windows PowerShell 5.1. Everything installed lives inside that one distro, so cleanup is a single wsl --unregister.
**Alternative rejected:** Running the kit's Install-SetuPrerequisites (Visual Studio Build Tools, Wireshark, a fresh Ubuntu 24.04; several GB on Windows, hard to remove cleanly), and installing the lab into my existing Ubuntu in place (cleanup not provable).
**Cost accepted:** The kit was written for PowerShell 7 and Ubuntu 24.04; I run 5.1 and 26.04, so a cmdlet may misbehave and I check each one as I use it. The containerlab apt repo is unsigned (trusted=yes), confined to setu-lab. The clone costs about 3 GB of disk.
**Evidence:** INSTALL-LEDGER.md, section "Week 0 verification" (Test-SetuHost all OK except optional mac802154_hwsim).
