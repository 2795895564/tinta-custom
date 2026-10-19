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

## Limits

- Hover-only styling is still subtle by design; keyboard selection is the
  primary state.
- Long names rely on DirectWrite trimming; they were not re-tested at narrow
  widths in this pass.
