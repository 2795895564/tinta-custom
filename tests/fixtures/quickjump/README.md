# Quick Jump Fixture

This repository exercises the Ctrl+K palette: file names, heading titles,
relative paths, folders and recents.

| Area | What to jump to |
| --- | --- |
| Notes | `alpha.md`, `beta.md` |
| Nested | `deep/gamma.md` |
| Archive | `archive/gamma-old.md` |

## Recent files

The empty query lists the most recently modified entries. Keep the files below
mixed so the palette has to show names and titles together:

- a table and a list in `notes/alpha.md`
- a quote and code in `notes/beta.md`
- **bold**, *italic* and a [link](https://example.com) in `deep/gamma.md`

> Recent order is by modification time, ties by natural path order.

```text
Ctrl+K -> type -> Enter -> open in a tab
```
