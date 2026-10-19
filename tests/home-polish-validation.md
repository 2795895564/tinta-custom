# Home polish validation

Screenshots live in `out/polish/`.

## Selection

- Select two rows with `Space`: both rows get a stronger tint and accent rail.
- The file name and metadata brighten together; the current row still has the
  stronger fill.
- Footer switches to `Ctrl+C copy  Ctrl+X cut  D delete  Esc clear`.
- Screenshot: `03-selected.png`.

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
