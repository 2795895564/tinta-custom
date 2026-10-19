# External change validation

## Home directory

1. Launch Tinta with a repository and keep the Home tab active.
2. From Explorer/PowerShell create `c.md` in the current directory.
3. Within about one second the list goes from 2 to 3 items.
4. The cursor stays on the same file (`a.md`) and multi-selection is preserved.
5. Delete or rename an entry externally and it disappears/updates after the
   next tick.

Screenshots: `out/watch/01-before.png`, `02-after-add.png`.

## Open document

The existing file watch still reloads the active document when its bytes change
outside Tinta. Unsaved edits raise the existing conflict prompt instead of
silently replacing the buffer.

## Limits

- The Home watcher keys off the directory's write time. This covers entries
  being created, removed or renamed.
- Editing a file's contents without changing the directory entry (for example
  saving in place) may change its size/time in the listing only at the next
  rescan/navigation; the open document itself is still reloaded by the
  document watcher.
- Large directories are not rescanned on a fixed interval; only a directory
  write-time change triggers the quiet rescan.
