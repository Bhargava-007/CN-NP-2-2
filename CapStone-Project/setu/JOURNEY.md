# JOURNEY

## Week 0: setup
**Decision:** Run the lab in a dedicated WSL2 distro "setu-lab" (a local clone of my existing Ubuntu 26.04), with Docker (docker.io) and containerlab inside it, driven by the kit's SetuLab module from Windows PowerShell 5.1. Everything installed lives inside that one distro, so cleanup is a single wsl --unregister.
**Alternative rejected:** Running the kit's Install-SetuPrerequisites (Visual Studio Build Tools, Wireshark, a fresh Ubuntu 24.04; several GB on Windows, hard to remove cleanly), and installing the lab into my existing Ubuntu in place (cleanup not provable).
**Cost accepted:** The kit was written for PowerShell 7 and Ubuntu 24.04; I run 5.1 and 26.04, so a cmdlet may misbehave and I check each one as I use it. The containerlab apt repo is unsigned (trusted=yes), confined to setu-lab. The clone costs about 3 GB of disk.
**Evidence:** INSTALL-LEDGER.md, section "Week 0 verification" (Test-SetuHost all OK except optional mac802154_hwsim).

## Week 1
**Decision:** Replaced the shipped `byte_stream.c` and `webget.c` with my own implementations in `stack\src\` (originals stay in git history, commit `e09d168`), and patched `Invoke-SetuWsl` so the PS7-targeted kit runs on Windows PowerShell 5.1. Captured packets at three taps and tested my ByteStream (1 MB through capacity 2, clean under ASan/UBSan) and `webget` against the kernel.
**Alternative rejected:** Keeping the shipped reference code and only reading it (rejected: I must defend my own code). Installing PowerShell 7 to avoid patching the kit (rejected: extra install, footprint, the patch was 8 lines).
**Cost accepted:** Kit targets PS7/Ubuntu 24.04 and I run 5.1/26.04, so I carry a local patch for PS 5.1. Stock `Invoke-SetuCapture` cannot write to `/mnt/c` as user `tcpdump`, so captures go to `/tmp` and are copied manually. The distro idles out and loses veth links, requiring a hidden `sleep infinity` keepalive and redeploy after shutdown instead of patching the cmdlet now.
**Measured:** RTT `dev01` to `srv01`, 3 runs of 1000 probes, median p50 `0.078 ms`, p99 `0.201 ms` (range `0.167-0.204`), `0%` loss; delay is processing, not transmission or propagation, which is the opposite of a real campus link.
**Lesson:** Wire efficiency of an 11 B payload is `14.3%` on the wire (COMPUTED).

## Week 2
**Decision:** Measured one impairment at a time (delay, loss, rate), always on both ends because `netem` is egress only, and confirmed baseline after each. Found that a packet-count queue limit does not bound delay and replaced it with a time-bounded `tbf` queue.
**Alternative rejected:** Trusting my own arithmetic for queue delay (rejected: three predictions failed; 50 packets held 14-19 KB each, MTU 9500, offload and GSO cap had no effect). Reporting means or single runs (rejected: percentiles, 3 runs, range).
**Cost accepted:** `BUDGET.md` v0 is NOT YET PROVEN: `200 Mbit/s` with an EF flow is unmeasured, only `100 Mbit/s`, 1 TCP flow, one direction shaped. Cause of large queued packets and of the `7 ms` plateau above the knee not identified. `Measure-SetuThroughput -Runs 1` fails under `StrictMode`, so some UDP/TCP throughput points are missing; the 1000-packet case has 1 run.
**Measured:** idle RTT `p50 0.09` / `p99 0.269 ms`; 1000-pkt queue loaded `p50 269 ms`; `tbf` loaded `p50 3.86` / `p99 5.0 ms` at `99.2 Mbit/s`; knee between `95M` and `100M` offered (`p50 0.06 -> 7.2 ms`).
**Lesson:** Bound queues in bytes or time, not packets.

## Week 3
**Decision:** Hierarchical address plan: one `/20` per site with VLSM inside, and `/64` per segment from a `/52` per site (4 site routes plus infra instead of about 19 with tight VLSM). The resource constraint is routes and operator comprehension, not addresses. Second decision: Kea is installed and started by hand after deploy while `kea01` idles on `sleep infinity`.
**Alternative rejected:** Tight VLSM packed from `10.20.0.0`, and boot-time `apt` install in the `kea01` container command.
**Cost accepted:** Low address efficiency (`COMPUTED` in `ADDRESS-PLAN.md`; `BLD-C` `22.9%` today, `36.7%` at 5 years) and most of the `/16` unused, with host counts as `ASSUMPTION`s. One extra manual step per deploy for Kea, not yet scripted.
**Measured:** Relayed DHCP lease works (`dev01` `10.20.32.20/23` from Kea `10.20.48.9` via `rtr`). Three faults reproduced with full hypothesis/test/evidence/fix/verify: missing relay (no `DISCOVER` in Kea log), wrong scope (`DISCOVER` arrives, `"No subnet matches relay address: 10.20.32.1"`), server down (`0` Kea processes, no lease); fault 3 has no wire capture (empty twice, cause unknown). `check_plan.py` passes `7/7` checks, proving self-consistency rather than that the host counts are right.
**Lesson:** Three of my own tests were invalid before they were valid (pool and reservation not moved with the subnet; `docker top` shows host PIDs while `kill` needs in-container PIDs; a stale Kea held port 67), so a lease or failure only counts as evidence after confirming which process answered. Also: Kea 2.2.0 syntax differs from newer docs.
