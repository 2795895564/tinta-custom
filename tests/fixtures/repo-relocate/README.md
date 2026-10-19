# Relocate Fixture

A small repository used to check that Tinta can repoint a missing repository
without losing its identity.

| Step | Expectation |
| --- | --- |
| Rename this folder | Picker shows `folder missing` |
| Press `r` on the row | Folder dialog opens |
| Choose the renamed folder | Home opens it; settings keep the same repo id |
| Restart | The repository still opens at the new path |

- `note.md` is here so Home has something to list.
- Relocating must never touch files on disk.
