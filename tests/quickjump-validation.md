# Quick Jump (Ctrl+K) validation

Fixtures live in `tests/fixtures/quickjump/`. They mix headings, tables, lists,
emphasis, links, quotes, fenced code, nested folders and a Chinese-named note so
the palette has to render names and titles together and the index has to keep
walking after a folder.

| Fixture | Purpose |
| --- | --- |
| `README.md` | newest recent row; mixed Markdown |
| `notes/alpha.md` | name + H1 title (`Alpha Notes`) and a table |
| `notes/beta.md` | title-only query (`beta plan`) |
| `deep/gamma.md` | nested result; shares `gamma` with the archive note |
| `deep/nested/epsilon.md` | deep path; nested folder row |
| `notes/this-is-a-very-long-document-name-...md` | long name must be trimmed inside the card |
| `archive/gamma-old.md` | older `gamma` result and the folder-jump target |
| `测试笔记.md` | CJK name and `中文标题` H1 |

## Automated checks

`tests/validate_quickjump.ps1` builds a portable copy beside a running Tinta,
launches it against the fixture, and writes screenshots to `out/quickjump/`:

1. `Ctrl+K` opens the palette over Home; Esc closes it and the Home pixels
   return to the baseline.
2. Typing `alpha notes` narrows to `alpha.md`; Enter opens it in a tab of the
   same window.
3. The palette opens over the reader; Esc returns to the reader, not Home.
4. Reader Esc still goes Home.
5. Typing `archive` and pressing Enter on the folder row navigates the fixed
   Home tab to `Root / archive`; Enter there opens `gamma-old.md`.
6. A mouse hover + click on the first recent row opens `README.md`.
7. In edit mode, `Ctrl+K` still opens; Esc returns to the same editing state;
   then typing `测试` and Enter opens `测试笔记.md` (CJK WM_CHAR path).
8. A 560x700 window keeps the card on screen and the footer hint and status do
   not overlap.
9. A repository with no supported files shows `No recent files yet`.
10. Renaming the repository after launch makes the palette show
    `Could not index this folder (error 3). Press F5 to retry.` plus the path.
11. A 2,500-file tree is captured while the first scan is still running
    (`Indexing workspace...`).
12. A very long file name is trimmed with an ellipsis inside the card;
    typing `trimmed` changes the list (WM_KEYDOWN -> TranslateMessage path).

`tests/quickjump_tests.cpp` covers the pure decisions: path equality,
generation/root rejection of stale results, clamped list movement, and the
empty/indexing/error/recent-empty/no-match state selection. The indexer test
starts two scans and checks that each result carries its own root and
generation.

## What was actually checked

- `tests/quickjump_tests.cpp`, `tests/workspace_index_tests.cpp`,
  `tests/i18n_tests.cpp` and `home_logic_tests` pass.
- `tests/validate_quickjump.ps1`: 19/19 checks, screenshots in
  `out/quickjump/` (normal, narrow, recent empty, index error, indexing, CJK,
  edit mode, reader and mouse-open states).
- Rendered at 1024x768 and 560x700 with the light theme. The card stays inside
  the client area; the narrow footer now measures the status text before
  drawing the hint, so the two cannot overlap.
- CJK text was entered with `PostMessageW(WM_CHAR)`; this verifies the 16-bit
  character path, not a real IME composition. The palette now has an IME path
  (composition window anchored at the query caret and `GCS_RESULTSTR` written
  in one batch), but the candidate-window position still needs a manual check
  with a real input method.

## Remaining limitations

- The indexing-state screenshot is timing-based evidence, not a hard assertion
  that the scan was still running at capture time.
- Mouse geometry in the script assumes 96 DPI; the app is tested at that scale.
- The palette lists the workspace index (recent = files in mtime order; folders
  appear only for a non-empty query). Open tabs and `settings.ini` recents are
  not merged into the list yet.
- There is no file watcher; a refresh happens on the next palette open (or F5
  after an error), not while the palette stays open.
