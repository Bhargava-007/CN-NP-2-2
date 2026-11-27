# SETU Campus Network Capstone

SETU is my 12-week networking capstone project. It is organized into three parts:

- **LINK** (`link/`): Campus network addressing, topology, DHCP/DNS services, and fault bank.
- **STACK** (`stack/`): C17 networking implementations (`byte_stream`, `webget`, `http_server`, `dns_resolve`, plus upcoming TCP/router modules).
- **EDGE** (`edge/`): Edge latency/jitter budget (`BUDGET.md`) and QoS verification.

## Repository Layout

- `SetuLab/` - PowerShell module (`SetuLab.psm1`) driving the WSL2 + containerlab lab engine.
- `link/design/` - `ADDRESS-PLAN.md`, `TOPOLOGY.md`, `POLICY.md`.
- `link/topology/` - Containerlab topologies (`w01-skeleton.clab.yml`, `w03-services.clab.yml`, etc.).
- `link/config/` - Kea DHCPv4 and BIND9 zone/config files.
- `link/diagnostics/` - Fault bank write-ups (`W03-DHCP-FAULTS.md`).
- `link/verify/` - Verification scripts and results (`check_plan.py`, `Start-W03Kea.ps1`, `W01-FIELD-DIFF.md`, `W01-DELAY-ACCOUNTING.md`).
- `stack/src/` - C17 source files and `platform.h`.
- `stack/tests/` - Unit tests and `test_dns_fake.py`.
- `edge/` - `BUDGET.md`.
- `INSTALL-LEDGER.md`, `JOURNEY.md` - Environment ledger and weekly engineering reflections.

## Bringing Up the Labs

Import the lab module from `CapStone-Project\setu`:

```powershell
Import-Module .\SetuLab\SetuLab.psm1
```

- **Week 1 (`w01`):** (not yet a single command)
  ```powershell
  Start-SetuLab .\link\topology\w01-skeleton.clab.yml
  New-SetuBridge sw-c eth1,eth2
  New-SetuBridge sw-dc eth1,eth2
  Set-SetuAddress dev01 eth1 10.20.40.10/24 10.20.40.1
  Set-SetuAddress rtr eth1 10.20.40.1/24
  Set-SetuAddress rtr eth2 10.20.50.1/24
  Set-SetuAddress srv01 eth1 10.20.50.10/24 10.20.50.1
  Enable-SetuRouting rtr
  ```
  These w01 addresses are lab-only and not part of ADDRESS-PLAN.md.
- **Week 3 (`w03-services`):**
  ```powershell
  Start-SetuLab .\link\topology\w03-services.clab.yml
  .\link\verify\Start-W03Kea.ps1
  ```

## Building and Testing `stack/`

Run from `CapStone-Project\setu` inside the `setu-lab` WSL2 distro (Linux):

```bash
mkdir -p build
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -g stack/src/http_server.c -o build/http_server
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -g stack/src/dns_resolve.c -o build/dns_resolve
```

With CMake (`stack/CMakeLists.txt`, Windows build, not yet tested):

```powershell
cmake -S stack -B stack/build ; cmake --build stack/build --config Debug ; ctest --test-dir stack/build -C Debug
```

Note: `stack/measure/costcurve-raw.txt` is superseded by `stack/measure/costcurve-c-k1.txt`, and the `server_epoll` CMake target builds the shipped reference server, not our own.

## Week-by-Week Status

| Week | Scope | Status |
| :--- | :--- | :--- |
| 0 | WSL2 `setu-lab` setup, Docker, containerlab, `Test-SetuHost` | Done & tested |
| 1 | `w01` captures, delay accounting, `byte_stream`, `webget` | Done & tested |
| 2 | `netem`/`tbf` bufferbloat, queueing knee, `BUDGET.md` v0 | Done & tested (throughput `-Runs 1` hit StrictMode bug) |
| 3 | `ADDRESS-PLAN.md`, `check_plan.py` (7/7), `w03-services` DHCP & Fault Bank, `TOPOLOGY.md`, `POLICY.md` | Done & tested (fault 3 wire capture missing) |
| 4 | `http_server.c`, `dns_resolve.c`, DNS forward/reverse zones, `test_dns_fake.py` | Done; Linux-tested (see Known gaps) |

## Known Gaps

- Wrong-source DNS reply rejection is not tested.
- Windows native builds (`cl` / CMake on Windows) are not yet tested.
- `link/verify/Start-W03Kea.ps1` is written, not yet tested end-to-end.
- Chunked request body handling in `http_server.c` under ASan/UBSan is not covered (test sent chunked `POST`, received `405`).
- DNS `TC=1` TCP fallback was tested only against `stack/tests/test_dns_fake.py`, not against a live BIND9 TCP response.

<!-- disclose AI use here if the course requires -->
Tools used: see course AI-use policy
