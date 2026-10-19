# Session restore validation

## Active file tab

Settings:

```ini
[Session]
sessionActive=1
tab=D:\...\repo\a.md
tab=D:\...\repo\b.md

[Repos]
startupMode=0
repoCurrent=r1
repo=r1|0|Session|D:\...\repo|
```

Launch normally with `openInTabs=1`.

Expected:

- Tab row: `Home`, `a.md`, `b.md`
- `b.md` is the active tab
- The document shows `Bravo`
- `Home` remains tab 0 and is not closed

Screenshot: `out/session/01-restored.png`.

## Active Home

Set `sessionActive=0` with the same tabs. Launch.

Expected:

- `Home` is the active tab
- `a.md` and `b.md` remain as background tabs

## Missing files

Remove `b.md` from disk and set it as the active tab. Launch.

Expected:

- `b.md` is dropped from the restored tab row
- The active slot falls back to `Home` instead of opening an empty document
