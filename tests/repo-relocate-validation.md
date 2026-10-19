# Repository relocation validation (P3)

The folder picker is a native Windows dialog, so this pass is manual. The pure
request path (`r` on a missing row) is covered by `home_logic_tests`.

## Setup

1. Copy `tests/fixtures/repo-relocate/` to a scratch folder, for example
   `out/repo-relocate/repo`.
2. Point a portable `settings.ini` at it:

   ```ini
   [Repos]
   startupMode=0
   repoCurrent=r1
   repo=r1|0|Relocate Fixture|<scratch>\repo|
   ```

3. Rename `<scratch>\repo` to `<scratch>\repo-moved` **before** launching
   Tinta, so the stored path is missing at startup.

## Checks

1. Launch Tinta. The repository picker is shown; the row is red and says
   `folder missing`; the selected row shows `missing · r`.
2. Press `r`. The system folder picker opens.
3. Choose `<scratch>\repo-moved`. The picker closes and Home opens
   `Root / repo-moved`; `note.md` is listed.
4. Open `note.md`; it renders the heading `Relocated Note`.
5. Close and relaunch. The same repository opens directly at the new path.
6. Inspect `settings.ini`: the repo line still starts with `r1|`, and its path is
   the new folder.
7. Repeat with a second repo already pointing at the chosen folder: press `r`
   and choose that folder. The picker stays open and shows
   `That folder is already a repository: <name>`; no path is changed.
8. Press `r` on a healthy row. Nothing opens; the footer explains that only a
   missing repository can be relocated.
9. Double-click a missing row. The same folder picker opens (mouse affordance).
10. Press `d` on a healthy row. `settings.ini` gets
    `repoCurrent=<id>` and `startupMode=0`; the row shows `default`.
11. Press `d` on a missing row. The footer says
    `A missing repository cannot be the startup repository.` and settings are
    unchanged.
12. Press `x` once: the footer asks to press X again. Press `x` again: the repo
    disappears from settings; the folder on disk is untouched. A first `x`
    followed by `j`/Enter/click cancels the pending removal.

## What is not automated

- Selecting a folder in the native `IFileOpenDialog`.
- The settings write-failure branch (`Repository relocated for this session,
  but settings could not be saved`). Simulate with a read-only settings
  directory if needed.

## Remaining work in P3

- A visible button for relocate/default, beyond the `r`/`d` keys and
  double-click. (The keyboard and double-click paths are complete.)
