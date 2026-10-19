# Home clipboard validation

Home uses transient selection plus an internal file clipboard; there are no
Dired marks or flags.

## Keys

```text
Ctrl+A        select the visible column
Ctrl+C        copy selected items (or the current row)
Ctrl+X        cut selected items
Ctrl+V        paste into the current directory
Esc           clear selection
```

The footer shows `N selected` and `N copied` / `N cut`.

## Checks

1. In a repo with `a.md`, `b.md`, `c.md` and `dest/`, press `Ctrl+A`.
   Every visible row is selected and the footer shows `3 selected`.
2. Press `Ctrl+C`. Footer shows `3 copied`.
3. Navigate into `dest/` and press `Ctrl+V`. Files appear there; the footer
   reports `Pasted 3 · skipped 0 · failed 0`. Press any next key (move, Space
   or Ctrl+C): the paste notice disappears and the normal selection/clipboard
   status returns.
4. Press `Ctrl+X` on `c.md`, go to `dest/`, press `Ctrl+V`. `c.md` disappears
   from the source and appears in `dest/`; the clipboard clears.
5. Paste with an existing name. The footer shows one line:
   `1 file(s) already exist: a.md   Enter: skip   O: overwrite   Esc: cancel`.
   There is no large dialog.
6. Enter keeps the old files and pastes the rest; O replaces same-named files;
   Esc cancels without pasting.
7. Try to copy/move a folder into itself or a descendant. It is skipped.
8. Ctrl+C/X/V in the editor still operate on text; the file clipboard only
   responds when Home's file list is the active surface.

## Automated evidence

`out/clipboard/`:

- `01-select-all.png` — Ctrl+A, `3 selected`
- `03-copied.png` — two files selected and copied
- `04-dest-pasted.png` — `Pasted 2 · skipped 0 · failed 0`, both files present
- `05-dest-cut.png` — cut file moved; source gone

The probe created `a.md`, `b.md`, `c.md`, copied `a/b` into `dest/`, then cut
`c.md` into `dest/` and verified the disk state.

## Multi-delete

- Select two rows with `Space`, press `D`/`Delete`.
- The confirmation card says `Move 2 items to the Recycle Bin?`.
- Enter deletes both; an unselected file remains.
- Failures are counted in the footer; the rest are still removed.
- Screenshots: `out/delete/01-selected.png`, `out/delete/02-confirm.png`.

## Overwrite probe

`out/prompt/`:

- `01-prompt.png` — footer prompt listing `a.md` with Enter/O/Esc
- Enter kept `dest/a.md` as `OLD`; O changed it to `NEW`

Folder merging on overwrite is intentionally not offered; conflicting folders
are skipped.

## Remaining

- Folder overwrite is not offered (files only).
- OS clipboard (`CF_HDROP`) interop is not wired yet.
