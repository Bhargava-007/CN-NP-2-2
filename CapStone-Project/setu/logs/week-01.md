# Week 1 log (2026-11-23)

## Patched Invoke-SetuWsl so native stderr does not throw on PS 5.1
- **Date:** 2026-11-23
- **What was asked:** Minimal patch to `Invoke-SetuWsl` in `SetuLab\SetuLab.psm1` so native stderr from `wsl.exe` does not throw under Windows PowerShell 5.1.
- **Why:** `SetuLab.psm1` sets `$ErrorActionPreference = 'Stop'` (line 8) and targets PowerShell 7+. On Windows PowerShell 5.1, `& wsl.exe ... 2>&1` converts stderr lines into `ErrorRecord` objects, which terminate execution when `$ErrorActionPreference` is `'Stop'`.
- **What changed:** Wrapped the `& wsl.exe ... 2>&1` call and `$LASTEXITCODE` capture in `SetuLab\SetuLab.psm1` (originally lines 34-35, now lines 34-41) with `$ErrorActionPreference = 'Continue'` inside a `try { ... } finally { $ErrorActionPreference = $prevEap }` block, converting output objects via `ForEach-Object { "$_" }`.
- **Verification:** Not tested (WSL/PowerShell execution not run here).
- **Files changed:** `CapStone-Project\setu\SetuLab\SetuLab.psm1`, `CapStone-Project\setu\logs\week-01.md`
- **Suggested commit message:** `fix(kit): handle native stderr in Invoke-SetuWsl on PS 5.1`
