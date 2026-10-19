# Inline new/rename validation

Home creates and renames entries in the list itself; there is no naming modal.
Delete confirmation remains a modal.

## Checks

1. Open a repository in Home.
2. Press `n`. A temporary row appears at the top of the active column with an
   empty name field.
3. Type `inline-new.md`; press Enter. The row disappears, the file exists, and
   the list refreshes with the new file selected.
4. Press `r` on a file. The row's name becomes an editable field; Enter commits
   the rename.
5. Press `n`, type `should-not-exist.md`, press Esc. No file is created and the
   list returns to normal.
6. Press `Shift+N`, type a folder name, press Enter. The folder is created and
   listed first.
7. Try an empty name and an existing name. The editor stays open and the footer
   shows the validation/error message.
8. With a Chinese IME, start a composition in either inline field. The
   composition window should follow the inline caret and the committed text
   should appear in the row.
9. Press `d` on a file. Delete still uses the confirmation card; inline editing
   must not affect it.

## Automated evidence

A probe against `tinta-inline.exe` checked:

- inline new created `inline-new.md`
- inline rename produced `renamed.md` and removed the old name
- Esc after typing `should-not-exist.md` created nothing
- screenshots in `out/inline/` (`01-inline-new.png`, `03-inline-rename.png`)

## Remaining limitations

- Caret placement by mouse is not implemented; keyboard Home/End/arrows work.
- Real IME candidate placement needs a manual check.
- Multi-select is not yet used by rename/copy/move; those commands arrive with
  the Dired-style batch operations.
