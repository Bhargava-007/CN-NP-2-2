# Week 1 log (2026-11-23)

## Patched Invoke-SetuWsl so native stderr does not throw on PS 5.1
- **Date:** 2026-11-23
- **What was asked:** Minimal patch to `Invoke-SetuWsl` in `SetuLab\SetuLab.psm1` so native stderr from `wsl.exe` does not throw under Windows PowerShell 5.1.
- **Why:** `SetuLab.psm1` sets `$ErrorActionPreference = 'Stop'` (line 8) and targets PowerShell 7+. On Windows PowerShell 5.1, `& wsl.exe ... 2>&1` converts stderr lines into `ErrorRecord` objects, which terminate execution when `$ErrorActionPreference` is `'Stop'`.
- **What changed:** Wrapped the `& wsl.exe ... 2>&1` call and `$LASTEXITCODE` capture in `SetuLab\SetuLab.psm1` (originally lines 34-35, now lines 34-41) with `$ErrorActionPreference = 'Continue'` inside a `try { ... } finally { $ErrorActionPreference = $prevEap }` block, converting output objects via `ForEach-Object { "$_" }`.
- **Verification:** Not tested (WSL/PowerShell execution not run here).
- **Files changed:** `CapStone-Project\setu\SetuLab\SetuLab.psm1`, `CapStone-Project\setu\logs\week-01.md`
- **Suggested commit message:** `fix(kit): handle native stderr in Invoke-SetuWsl on PS 5.1`

## Created W01-FIELD-DIFF.md from measured capture output
- **Date:** 2026-11-23
- **What was asked:** Create `link/verify/W01-FIELD-DIFF.md` with the UDP request and ICMP port-unreachable reply field-diff tables and observations.
- **What happened:** Created `CapStone-Project\setu\link\verify\W01-FIELD-DIFF.md`.
- **Notes / operational findings:**
  - Stock `Invoke-SetuCapture` writes nothing on this setup (`tcpdump` runs as user `tcpdump` and cannot write to `/mnt/c`, with errors hidden), so captures were taken to `/tmp` and copied.
  - The WSL distro must be kept alive with a hidden `sleep infinity` or containers restart and lose links.
- **Files created/changed:** `CapStone-Project\setu\link\verify\W01-FIELD-DIFF.md`, `CapStone-Project\setu\logs\week-01.md`
- **Suggested commit message:** `docs(w01): add field-diff table and capture notes`

## Replaced ByteStream with own ring-buffer implementation (CP0)
- **Date:** 2026-11-23
- **What was asked:** Replace the shipped reference `byte_stream.h` and `byte_stream.c` with own C17 ring-buffer implementation.
- **Why:** Own implementation replaces the shipped reference; the shipped originals remain in git history (commit `e09d168`).
- **Verification:** Not tested (no compiler/WSL run here).
- **Files changed:** `CapStone-Project\setu\stack\src\byte_stream.h`, `CapStone-Project\setu\stack\src\byte_stream.c`, `CapStone-Project\setu\logs\week-01.md`
- **Suggested commit message:** `feat(cp0): implement ring-buffer ByteStream`
