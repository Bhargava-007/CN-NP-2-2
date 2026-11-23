# JOURNEY

## Week 0: setup (DRAFT, student to edit)
**Decision:** Run the lab in a dedicated WSL2 distro "setu-lab" (a local clone of my existing Ubuntu 26.04), with Docker (docker.io) and containerlab inside it, driven by the kit's SetuLab module from Windows PowerShell 5.1. Everything installed lives inside that one distro, so cleanup is a single wsl --unregister.
**Alternative rejected:** Running the kit's Install-SetuPrerequisites (Visual Studio Build Tools, Wireshark, a fresh Ubuntu 24.04; several GB on Windows, hard to remove cleanly), and installing the lab into my existing Ubuntu in place (cleanup not provable).
**Cost accepted:** The kit was written for PowerShell 7 and Ubuntu 24.04; I run 5.1 and 26.04, so a cmdlet may misbehave and I check each one as I use it. The containerlab apt repo is unsigned (trusted=yes), confined to setu-lab. The clone costs about 3 GB of disk.
**Evidence:** INSTALL-LEDGER.md, section "Week 0 verification" (Test-SetuHost all OK except optional mac802154_hwsim).

## Week 1 (DRAFT, student to edit)
**Decision:** Replaced the shipped `byte_stream.c` and `webget.c` with my own implementations in `stack\src\` (originals stay in git history, commit `e09d168`), and patched `Invoke-SetuWsl` so the PS7-targeted kit runs on Windows PowerShell 5.1. Captured packets at three taps and tested my ByteStream (1 MB through capacity 2, clean under ASan/UBSan) and `webget` against the kernel.
**Alternative rejected:** Keeping the shipped reference code and only reading it (rejected: I must defend my own code). Installing PowerShell 7 to avoid patching the kit (rejected: extra install, footprint, the patch was 8 lines).
**Cost accepted:** Kit targets PS7/Ubuntu 24.04 and I run 5.1/26.04, so I carry a local patch and may meet more incompatibilities. The stock `Invoke-SetuCapture` writes nothing here (`tcpdump` runs as user `tcpdump` and cannot write to `/mnt/c`), so captures go to `/tmp` and are copied manually. The distro idles out and restarts containers, losing the veth links, so I need a hidden `sleep infinity` keepalive and a redeploy after each shutdown. Rejected: patching the cmdlet now (workaround is enough for week 1).
**Measured:** RTT `dev01` to `srv01`, 3 runs of 1000 probes, median p50 `0.078 ms`, p99 `0.201 ms` (range `0.167-0.204`), `0%` loss; delay is processing, not transmission or propagation, which is the opposite of a real campus link.
**Lesson:** Wire efficiency of an 11 B payload is `14.3%` on the wire (COMPUTED).
