# Large folder and failure validation

## 10k entries

- Created 10,000 empty `.md` files in `out/large/repo/big`.
- Opened the folder from Home.
- Worker thread scanned it; the active column showed `10000 items`.
- j/k remained responsive and the scrollbar appeared.
- Screenshots: `out/large/01-large.png`, `02-after-j.png`.

## Loading

- While the active directory scan is in flight the empty column says
  `Loading…` instead of looking like a read failure.
- `home.loading` is localized.

## Failed refresh

- A failed refresh for the current directory does not replace the last good
  listing.
- The footer says `Couldn't refresh · showing the last loaded contents`.
- `home_logic_tests` covers the stale-listing path: entries, cursor and
  multi-selection survive `homeApplyListing(..., ok=false)`.

## Limits

- The 10k test used small empty files; very large files with expensive metadata
  or network shares were not measured.
- Sorting still runs on the UI thread after the worker returns; 10k was
  comfortable, 100k was not tested.
