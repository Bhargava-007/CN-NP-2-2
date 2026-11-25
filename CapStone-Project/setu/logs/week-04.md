# Week 4 log (2026-11-25)

## Replaced http_server.c with own HTTP/1.1 implementation (CP1a)
- **Date:** 2026-11-25
- **What happened:** own `http_server.c` written replacing shipped starter, nothing tested.
- **Files changed:** `CapStone-Project\setu\stack\src\http_server.c`, `CapStone-Project\setu\logs\week-04.md`
- **Suggested commit message:** `feat(cp1a): implement HTTP/1.1 server in http_server.c`

## Created link/verify/Start-W03Kea.ps1
- **Date:** 2026-11-25
- **What happened:** `Start-W03Kea.ps1` written, nothing tested.
- **Files created/changed:** `CapStone-Project\setu\link\verify\Start-W03Kea.ps1`, `CapStone-Project\setu\logs\week-04.md`
- **Suggested commit message:** `feat(w04): add Start-W03Kea.ps1 helper for kea01 post-deploy setup`

## Replaced dns_resolve.c with own DNS resolver (CP1b)
- **Date:** 2026-11-25
- **What happened:** own `dns_resolve.c` written replacing shipped, nothing tested.
- **Files changed:** `CapStone-Project\setu\stack\src\dns_resolve.c`, `CapStone-Project\setu\logs\week-04.md`
- **Suggested commit message:** `feat(cp1b): implement DNS resolver in dns_resolve.c`

## Reconciled DNS zone and added reverse zone
- **Date:** 2026-11-25
- **What happened:** zone reconciled to plan, reverse zone added, serial bumped, nothing tested.
- **Files created/changed:** `CapStone-Project\setu\link\config\db.setu.local`, `CapStone-Project\setu\link\config\db.48.20.10.in-addr.arpa`, `CapStone-Project\setu\link\config\named.conf.local`, `CapStone-Project\setu\link\topology\w03-services.clab.yml`, `CapStone-Project\setu\logs\week-04.md`
- **Suggested commit message:** `chore(w04): reconcile DNS zone to ADDRESS-PLAN and add reverse zone`
