# Home polish validation

Screenshots live in `out/polish/`.

## Selection

- Cursor-only row: neutral fill plus a thin accent outline, no rail.
- `Space` marks a row: accent fill plus a left rail.
- Two adjacent marked rows keep a 1px gap, so they do not merge.
- Moving the cursor to an unmarked row leaves the marked rows intact and the
  cursor row neutral.
- Footer switches to `Ctrl+C copy  Ctrl+X cut  D delete  Esc clear`.
- Screenshots: `out/select/01-cursor.png`, `02-two.png`, `03-cursor-next.png`.

## Empty folder

- Enter a folder with no entries.
- The active column says `Empty folder  ·  n new file`.
- No stale row remains from the previous folder.
- Screenshot: `01-empty.png`.

## Search without matches

- Press `/`, type a query that matches nothing.
- The column says `No matches  ·  Esc clear`, and the item count is `0`.
- Esc clears the query and restores the listing.
- Screenshot: `02-no-matches.png`.

## Read failure

- Point the folder at a path that cannot be listed.
- The column says `Can't read this folder` instead of looking empty.

## Long names and narrow windows

- A 60+ character file name and a long folder name ellipsize before the size
  and time columns; they do not overlap or push metadata off the column.
- At 520 DIP the metadata columns remain readable; the footer shows the full
  selected name, trimming only at the edge.
- Trying to resize to 400px stops at the 520×420 DIP minimum.
- Screenshots: `out/longname/01-normal.png`, `02-narrow.png`.

## Dialog buttons

- The focused button is filled with the ink colour and uses background-coloured
  text; the unfocused button is an outline.
- A focused destructive action keeps a small red bar, so danger is still visible
  without the whole button using the selection accent.
- Delete confirmation: Left/Right/Tab switches Cancel/Delete; Enter runs the
  focused action; Esc still cancels.
- Paste-conflict card: the same focus style follows the mouse and Left/Right.
- Screenshots: `out/dialog2/01-delete-focused.png`,
  `02-cancel-focused.png`.

## Notice timing

- Pasting a file shows `Pasted 1 · skipped 0 · failed 0` in the footer.
- After about four seconds the notice disappears and the footer returns to the
  selected item / clipboard summary.
- Pressing any command key clears a success notice immediately.
- Error notices (`repoNotice`) still stay until the next state change.
- Screenshots: `out/notice/01-notice.png`, `02-cleared.png`.

## File-type badges

- Selected rows lift their leading badge: `MD` becomes a solid accent chip with
  inverted text; `IMG`/`TXT` become dark chips with inverted text.
- Unselected rows keep the pale badge so selected rows still read first.
- Folder chevrons switch to the text colour on a selected row.
- Screenshot: `out/badge/01-selected-badges.png`.

## Column hierarchy

- Opening a folder keeps the parent column visible but dims its names, badges,
  chevrons and metadata; the active column stays at full contrast.
- The effect is intentionally moderate (around 58% for inactive names) so the
  parent is still readable.
- Screenshot: `out/meta/01-columns.png`.

## Full-screen widths

- Tested maximized on a 2560×1540 work area, not just a 1024×768 window.
- Single column: 1200 DIP cap, centered; much less empty side margin than before.
- Two columns: 560 DIP each; three columns: 420 DIP each; group stays centered.
- Narrow windows still clamp to the available width.
- Screenshots: `out/full/01-full-root.png`, `02-full-two.png`,
  `03-full-three.png`.

## Limits

- Hover-only styling is still subtle by design; keyboard selection is the
  primary state.
- There is no hover tooltip for a truncated name; the footer and rename field
  are the full-name views.
