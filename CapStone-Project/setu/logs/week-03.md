# Week 3 log (2026-11-25)

## Created ADDRESS-PLAN.md
- **Date:** 2026-11-25
- **What was asked:** Write `link/design/ADDRESS-PLAN.md` with resource constraint, inputs/sizing, IPv4 site blocks and segment allocations, IPv6 `/48`->`/52`->`/64` plan, address efficiency, summaries, and open items.
- **Verification:** `ADDRESS-PLAN.md` written, nothing tested, hand arithmetic only, checker script comes next.
- **Files created/changed:** `CapStone-Project\setu\link\design\ADDRESS-PLAN.md`, `CapStone-Project\setu\logs\week-03.md`
- **Suggested commit message:** `docs(w03): add SETU-LINK ADDRESS-PLAN.md`

## Created link/verify/check_plan.py
- **Date:** 2026-11-25
- **What was asked:** Write `link/verify/check_plan.py` (Python 3 stdlib only) to validate all 7 checks on `link/design/ADDRESS-PLAN.md`.
- **Verification:** `check_plan.py` written, nothing tested.
- **Files created/changed:** `CapStone-Project\setu\link\verify\check_plan.py`, `CapStone-Project\setu\logs\week-03.md`
- **Suggested commit message:** `feat(w03): add check_plan.py address plan validator`

## Reconciled w03-services.clab.yml and kea-dhcp4.conf to ADDRESS-PLAN.md
- **Date:** 2026-11-25
- **What happened:** `w03-services` and `kea-dhcp4.conf` reconciled to `ADDRESS-PLAN`, nothing tested.
- **SetuLab.psm1 findings (read-only):**
  - `Start-SetuDhcpRelay` parameters: `-Node` (Mandatory), `-ClientInterface` (Mandatory), `-UpstreamInterface` (Mandatory), `-Server` (Mandatory).
  - Relay node: whichever `-Node` is passed (`rtr` in `w03-services`).
  - Relay tool: `dhcrelay` (`dhcrelay -4 -id $ClientInterface -iu $UpstreamInterface $Server`); if `dhcrelay` is missing, it attempts `apk add --no-cache dhcrelay` or `apt-get install -y -q isc-dhcp-relay`. Whether `frrouting/frr:latest` has `dhcrelay` preinstalled is unverified here.
  - `Invoke-SetuDhcpLease` parameters: `-Node` (Mandatory), `-Interface` (default `'eth1'`); flushes the interface and runs `udhcpc -i $Interface -n -q -t 4`.
- **Files changed:** `CapStone-Project\setu\link\topology\w03-services.clab.yml`, `CapStone-Project\setu\link\config\kea-dhcp4.conf`, `CapStone-Project\setu\logs\week-03.md`
- **Suggested commit message:** `chore(w03): reconcile w03-services and kea-dhcp4.conf to ADDRESS-PLAN`

## Fixed kea01 apt hang in w03-services.clab.yml
- **Date:** 2026-11-25
- **What happened:** `kea01` apt hang fixed with `force-confold` + `noninteractive`, nothing tested.
- **Why:** Bind-mounted `/etc/kea/kea-dhcp4.conf` triggers a dpkg conffile prompt that hangs `apt-get`.
- **Files changed:** `CapStone-Project\setu\link\topology\w03-services.clab.yml`, `CapStone-Project\setu\logs\week-03.md`
- **Suggested commit message:** `fix(w03): prevent dpkg conffile prompt hang in kea01`

## Replaced sleep 3 with eth1 wait loop in kea01
- **Date:** 2026-11-25
- **What happened:** `kea01` waits for `eth1` instead of `sleep 3`, nothing tested.
- **Files changed:** `CapStone-Project\setu\link\topology\w03-services.clab.yml`, `CapStone-Project\setu\logs\week-03.md`
- **Suggested commit message:** `fix(w03): wait for eth1 in kea01 instead of fixed sleep`

## Switched kea01 to sleep infinity
- **Date:** 2026-11-25
- **What happened:** `kea01` now idles on `sleep infinity`; Kea installed and started after deploy; nothing tested.
- **Files changed:** `CapStone-Project\setu\link\topology\w03-services.clab.yml`, `CapStone-Project\setu\logs\week-03.md`
- **Suggested commit message:** `fix(w03): idle kea01 on sleep infinity to stop crash-restart loop`

## Fixed Kea 2.2.0 logger key in kea-dhcp4.conf
- **Date:** 2026-11-25
- **What happened:** `kea-dhcp4.conf`: `output-options` -> `output_options` for Kea 2.2.0, nothing tested.
- **Files changed:** `CapStone-Project\setu\link\config\kea-dhcp4.conf`, `CapStone-Project\setu\logs\week-03.md`
- **Suggested commit message:** `fix(w03): use output_options in kea-dhcp4.conf for Kea 2.2.0`

## Created link/diagnostics/W03-DHCP-FAULTS.md
- **Date:** 2026-11-25
- **What happened:** `W03-DHCP-FAULTS.md` written from pasted evidence; fault 3 wire capture missing; nothing tested by the IDE.
- **Files created/changed:** `CapStone-Project\setu\link\diagnostics\W03-DHCP-FAULTS.md`, `CapStone-Project\setu\logs\week-03.md`
- **Suggested commit message:** `docs(w03): add DHCP fault bank from measured runs`
