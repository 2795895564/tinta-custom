# Caret centering and preview block follow — validation

The source editor keeps the edited line at a configurable height (50% by
default) of its viewport. The docked preview places the caret's Markdown
block near the top of the page, falls back to page-top sync when the source
is scrolled away, and pauses while the preview scrollbar or a table cell is
in use.

## Automated

```powershell
ctest --test-dir build -C Release -R "preview_follow|docked_preview_and_hints|editor_features|wrapped_cursor_navigation|editor_context_selection|search_input_focus" --output-on-failure
```

The `preview_follow` suite loads `tests/fixtures/preview-follow.md`, lays it
out, and checks:

- top-level anchors carry `blockStart`, and the caret block has ordered bounds;
- a short block is placed at the preview top, a tall block keeps the caret
  line in view, and the target clamps at the document end;
- the editor centers the caret at 50% by default and follows a changed ratio;
- turning centering off leaves the centering helper inert;
- a caret move enters `Caret`, source scrolling enters `Page`, and the next
  caret move resumes `Caret`;
- dragging the preview scrollbar enters `Manual` and leaves `scrollY` alone
  until the caret moves;
- a table cell editor pauses follow and committing the cell resumes it;
- later lines inside the code block map further down the rendered page.

The existing `docked_preview_and_hints`, `editor_features`,
`wrapped_cursor_navigation` and `editor_context_selection` suites keep their
caret-geometry contract by disabling centering in their own setup.

## Manual check

1. Build `build/Release/tinta.exe`.
2. Open `tests/fixtures/preview-follow.md`, press `E`, and wait for the split
   preview.
3. Type continuously: the edited line stays around the middle of the source
   viewport.
4. Press Enter repeatedly and paste 20+ lines: the caret keeps its position
   and the preview follows the newest block.
5. Use arrows, Home/End, PageUp/PageDown: the caret returns to the configured
   height; only the very top of the document clamps (there is no negative
   scroll), while the virtual bottom space lets the last line center too.
6. Click at the top, middle and bottom of the source: the clicked line moves
   to the configured height.
7. Use Find/Replace: the match lands at the configured height and the preview
   shows its block.
8. Open a one-line document: the line stays at the top (no negative scroll);
   the virtual bottom space may show a scrollbar.
9. Long code block, long list and wide table: the preview keeps the edited
   area visible (the tall-block path uses the caret line near 25%).
10. Scroll the source wheel while leaving the caret alone: preview follows the
    page; click/type returns it to the caret block. Image and diagram reflows
    alone must not switch to page mode.
11. Drag the preview scrollbar: the page stays where it is dragged until the
    caret moves.
12. Paste an image paragraph. While the image decodes/relayouts, the preview
    keeps its top source line instead of scrolling by itself.
13. Edit a table cell in the rendered preview: the page does not jump; after
    committing, it follows again.
14. Toggle Settings → Editor → **Center the caret** off: scrolling returns to
    the old edge-only behaviour; on returns centering.
15. Drag Settings → Appearance → **Caret position** between 20% and 80%: the
    editor moves immediately; the preview block target is unaffected.
16. Resize the window, drag the split seam and Ctrl+wheel zoom: the caret
    stays at the configured height.
17. Restart Tinta: both settings persist; a config without them loads the
    defaults (centering on, 50%).
18. Undo/redo, text selection, IME input and multi-caret editing continue to
    work; the scroll follows the primary caret only.
19. Repeat at normal (1050×900) and narrow (650×600) sizes, Paper and
    Midnight, word wrap on and off.

## Notes

Centering is implemented in `editorPinCaret()` in `src/editor.cpp`; it reuses
`editorPosRowY()` so wrapped lines use the real DirectWrite layout.
`editorCenteringBottomPad()` adds virtual space below the last line so the
final line can still reach the configured height; the top edge cannot center
without negative scroll. Preview follow is in `src/preview_follow.cpp` and
reuses the existing scroll anchors. Continuous typing re-arms the reparse at
most once per 200ms instead of postponing it forever, and image/diagram
reflows keep the preview's top source line. No extra Markdown parse or second
layout is performed per frame.
