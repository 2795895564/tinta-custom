# Feature validation workflow

- For every feature or compatibility extension, add or update runnable Markdown files in `tests/fixtures/` that demonstrate the new behavior.
- Include mixed documents: combine the feature with existing headings, tables, lists, emphasis, links, quotes and code where applicable. Check surrounding content as well as the feature itself.
- Exercise the fixtures in the built Tinta app using computer use or the native rendering/export harnesses. For layout changes, inspect rendered output at normal and narrow widths, and check relevant themes and exports. Record what was actually checked and any remaining limitations.
- Add meaningful automated regression checks for parser structure or layout failures found in these documents. Keep generated screenshots and exports in an ignored build/output directory.
- When working on a GitHub issue, reference it in completed commits. Do not automatically reply to the reporter; provide a draft reply for the maintainer when finished.

# Agent operating limits (hook)

Every tool call must be bounded. Do not repeatedly read large files.

| Item | Limit | On exceeding |
| --- | --- | --- |
| Single file read | <= 200 lines; prefer `offset`/`limit` | Stop; locate with `grep` first |
| Same file re-read | 1 unless the file changed | Stop; re-check the requirement |
| `grep` output | <= 50 matches, matching lines / small context only | Narrow the pattern or path |
| Build output | only `error C` / `error LNK` / `error MSB`, <= 40 lines | Do not paste the whole build log |
| Test output | summary and failing items only | Do not run unrelated full suites |
| Tool calls per task | soft cap 30 | Stop, restate the requirement, propose a shorter path |
| No new information | 3 consecutive calls circling the same spot | Stop immediately and reassess |

Working rules:

1. Locate first (`grep`/`glob`), then read a small range; never dump a whole file.
2. One file's relevant slice per read; batch independent calls.
3. Build/validate once per batch of changes; run only affected targets.
4. Over the limit: stop, restate the understood requirement, confirm there is no misunderstanding, then choose the simplest path or ask.
5. Prefer the smallest change: reuse existing interfaces; do not add abstractions without need; assert with scripts instead of inspecting many screenshots.
6. Do not re-verify completed work; do not re-read or re-run what already passed.
7. If a task exceeds the budget, report where it is stuck, what has been spent, and the recommended next step instead of continuing to consume tokens.

# Execution discipline (read first every run; lessons from real failures)

- **Time budget**: a single tool call over ~2 minutes goes to the background and I keep working; if nothing verifiable has been delivered for ~10 minutes, stop and report instead of grinding.
- **Subagents**: a subagent task must fit in ~10 minutes and must have an intermediate deliverable (build or test checkpoint). If it is still running at ~8 minutes, ask it for a checkpoint; if it cannot show one, interrupt it and take the work back. Never hand a whole multi-hour refactor to one subagent.
- **Big changes** (deleting a feature, touching 10+ files): back up first (`git diff` + untracked files into `build-verify/backup/`), then work in phases (entry points/shortcuts -> rendering and state -> tests and docs), and keep the build green at the end of every phase.
- **GUI screenshots**: use PowerShell `.NET Process.Start` + `SetProcessDpiAwarenessContext(-4)` + `PrintWindow(hwnd, hdc, 2)`. `Start-Process` hangs in this environment; never use it for the app.
- **Build**: use `D:\env\CMake\bin\cmake.exe`; build `--target tinta` and show only `error C/LNK/MSB`, then run only the affected tests. Do not run the full ctest suite blindly.
- **Access denied**: decide whether it is a sandbox write boundary or an ACL problem before retrying. Under `workspace-write`, `build/` may be read-only; do not repeat the same denied command.
- **No re-verification**: do not re-read or re-run what already passed, and do not re-take screenshots that already exist and are current.
- **Visible delivery**: every wait must end in something the user can check — build output, test result, or a screenshot; otherwise stop and say why.
