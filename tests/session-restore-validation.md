# Session restore validation

## Active file tab

Settings:

```ini
[Session]
sessionActive=1
sessionHomeActive=0
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

Set `sessionActive=0` and `sessionHomeActive=1` with the same tabs. Launch.

Expected:

- `Home` is the active tab
- `a.md` and `b.md` remain as background tabs

## Crash recovery

1. Launch with `openInTabs=1`.
2. Open `b.md` so it becomes the active tab.
3. Wait at least five seconds (the session timer is 5s).
4. Kill the process instead of closing the window.
5. `settings.ini` contains:

   ```ini
   [Session]
   sessionActive=0
   sessionHomeActive=0
   tab=D:\...\repo\b.md
   ```

6. Relaunch: `b.md` is the active tab and shows `Bravo`.

Screenshots: `out/watch/03-active-b.png`, `04-restored.png`.

## Missing files

Remove `b.md` from disk and set it as the active tab. Launch.

Expected:

- `b.md` is dropped from the restored tab row
- The active slot falls back to `Home` instead of opening an empty document
