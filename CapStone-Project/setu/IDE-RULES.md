# IDE RULES (standing instructions for the AI IDE)
1. Every time you do any work in this repo, append an entry to logs/week-NN.md for the current course week (NN = two digits; week 0 is setup). Create the file if it does not exist.
2. Each entry has: date, what was asked, what was run (exact commands), what happened (key output, not full dumps), what failed and how it was fixed, what was measured (with units and conditions), files created or changed, and the commit message the student should use.
3. Never delete or rewrite earlier entries; only append. Record mistakes and dead ends too.
4. Never write measurements you did not see. If something is an estimate or a guess, label it as one.
5. JOURNEY.md is the student's graded document: do not edit it unless explicitly asked. Use logs/ for everything else.
6. You cannot run git here (the .git folder is outside the sandbox). Never run git; tell the student which paths to commit.
7. Do not use WSL, winget or installers. Stop and report if a task needs them.
