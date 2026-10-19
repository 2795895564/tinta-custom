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
5. Paste with an existing name. A compact card (430×200) shows
   `Name conflicts`, the colliding names, and
   `Enter: skip   O: overwrite   Esc: cancel`.
6. Enter keeps the old files and pastes the rest; O replaces same-named files;
   Esc cancels without pasting. The Skip button is neutral, Overwrite is red,
   and the selected one has a clear focus ring.
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

## Undo

`Ctrl+Z` after a successful paste:

- Move: each moved item is moved back from target to source. If the source
  path exists again, that item fails rather than being overwritten.
- Copy: each copied target is sent to the Recycle Bin; the source stays.
- Overwriting pastes are not undoable (the previous bytes are gone).
- New/rename/delete clear the undo journal.
- No journal: footer says `Nothing to undo`.

Probe (`out/undo/`): moving `a.md` to `dest/` then Ctrl+Z restored
`repo/a.md`; copying `b.md` then Ctrl+Z removed `dest/b.md` while `repo/b.md`
stayed.

## Overwrite probe

`out/prompt/`:

- `02-compact-popup.png` — compact conflict card listing `a.md`/`b.md`
- Enter kept `dest/a.md` as `OLD`; O changed it to `NEW`

Folder merging on overwrite is intentionally not offered; conflicting folders
are skipped.

## Remaining

- Folder overwrite is not offered (files only).
- OS clipboard (`CF_HDROP`) interop is not wired yet.
