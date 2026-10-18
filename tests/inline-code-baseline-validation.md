# Inline code on the text baseline (#259, #262)

Validated on Windows with the Release build on 2026-10-16.

## Reports

- #259: inline code sits lower than the body text beside it, shown with
  `Tinta测试`Inline Code`` in Chinese text.
- #262 (the same problem): inline code is offset from the body by roughly
  half a letter height.

## Cause

- An inline code span was centred vertically in the line by font size, not
  set on the text baseline. Code and body fonts have different ascents
  (Consolas or Cascadia beside Segoe UI, Georgia or Microsoft YaHei), so the
  code dropped below or rose above the words beside it.
- DirectWrite hangs each run from the top of its line by the ascent of the
  fonts it actually uses. A run drawn partly in a fallback font (Chinese
  beside a Latin body font, emoji) took that font's taller ascent and moved
  down against its neighbours.
- Emphasis inside a heading used the body-size bold and italic faces. Once
  code was aligned to its own run's format, code inside `**`npm`**` in a
  heading would have moved to the body baseline, near the top of the heading
  line. The review of the first fix found this.

## Change

- Every text run is placed so its own first-line baseline lands on the Latin
  baseline of its format. Inline code, wrapped long code spans and the
  source shown for unsupported TeX sit on the baseline of the text format
  around them.
- Each format's baseline is measured once from an "Ag" layout (the probe
  inline math already used) and cached on the App. The cache is cleared
  whenever the text formats are rebuilt, because a new format can reuse a
  freed address.
- Emphasis inside a heading keeps the heading's size, through new bold and
  bold-italic heading faces.

## Fixtures and automated checks

`fixtures/inline-code-baseline-259.md` has the reporter's line plus code in
plain text, a heading, `# Install **`npm`** now`, `## The *`config`* file`,
a list, bold and italic text, a quote, a table with a Chinese cell, a link, a
long code span that wraps, unsupported TeX `$\frac{1}$`, emoji beside code
and a fenced block.

`superscript_and_learn` lays the fixture out in Paper and Midnight, with the
theme fonts and four font pairs (Segoe UI/Courier New, Georgia/Lucida
Console, Microsoft YaHei UI/Cascadia Mono, SimSun/Consolas), at 1x and 2x,
650 and 1050 px. It checks:

- every inline code run outside the fenced block shares the baseline of each
  text run on its line, within 0.6 px (at least 16 code runs and 16
  comparisons per layout);
- every Latin-only text run is drawn exactly where its own font puts it,
  which pins the reference that code is aligned to;
- italic words in a heading keep the heading's size.

Each check fails when its part of the change is removed:

| Change disabled | Failures |
| --- | --- |
| Whole fix (code centred by size again) | 40 |
| Fallback-font correction only | 8 |
| Heading emphasis faces | 80 |
| Unsupported-TeX alignment | 40 |
| Reference baseline scaled by 1.25 | 40 |
| Reference baseline set to 0 | 76 |

The first two rows were measured before the heading, TeX and absolute checks
were added.

The full suite passed 36 of 36 after a clean rebuild.

## Desktop check

The fixture was opened in the built app in Paper and Midnight at 1000 and
650 px (`out/issue-259/after2/`, ignored). The default theme and the reporter's
fonts were checked earlier, in Arctic with Microsoft YaHei UI and Cascadia
Mono at 2x, Paper, and Georgia with Lucida Console (`out/issue-259/after/`).
Code sits on the line's baseline in every case. That includes the
reporter's `Tinta测试`Inline Code``, the table's Chinese cell, and code inside
emphasis in the two headings.

## Exports

The master build (c58981a) and this build exported HTML, DOCX and print pages
in Paper and Midnight. The documents were `markdown-regression-control`,
`footnotes`, `table-cells-246`, `math-mixed-layout`, `superscript-subscript`
and the new fixture.

- HTML and Word output were byte-identical.
- Print pages changed only where inline code, the unsupported-TeX fallback
  or heading scripts (#263) moved onto the baseline. The comparison is in
  `out/export-compare/print-diffs-before-after.png` (ignored).

## Limits

- The Latin baseline comes from each format's primary font. A line set
  entirely in a fallback font sits on that same baseline, which is where its
  neighbours on the line are.
- Inline code keeps its smaller code size inside headings. Only its baseline
  follows the heading.

## Draft replies (not posted)

### #259

Thanks for the report. Inline code was centred in the line by font size
rather than set on the text baseline, so with Chinese text and many font
pairs it sat lower than the words beside it. The next release puts every
code span, including ones inside bold, italic, tables and headings, on the
baseline of the text around it, with any body and code font. Your
`Tinta测试`Inline Code`` example now lines up.

### #262

Thanks. This is the same bug as #259: inline code was centred by font size
instead of sitting on the text baseline. It's fixed for the next release,
for every theme and font pair. I'll close this one as a duplicate of #259 so
both are tracked in one place.
