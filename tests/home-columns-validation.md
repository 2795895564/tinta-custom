# Home B Miller columns · layout refinement validation

## What changed

- Responsive column widths in DIPs: one column is 800 DIP, grows to 58% of the
  window and caps at 900 DIP; two columns prefer 520 DIP and three prefer
  400 DIP. The group stays centred and every column shrinks to fit narrow
  windows instead of overflowing.
- Rows are 34 DIP tall with 18 DIP horizontal padding, 13.5 DIP names, a
  leading folder chevron or file-type badge (`MD`, `IMG`, `PDF`, ...), a 2 px
  accent bar on the selected row, a stronger hover wash, right-aligned size
  and modified time at three brightness levels, and a thin scroll rail for
  overflowing columns.
- Column sheets are rounded and lifted with a soft shadow. The active column
  gets an accent border; inactive columns keep a faint hairline.
- The top bar is 48 DIP tall. The repository chip has an accent dot, the
  breadcrumb shows only the path inside the repository (`Root / projects /
  design`), and the 320 DIP search box has a drawn magnifier icon.
- The column header reads `folder · N items` (localised, singular aware);
  a single column shows only the item count so it does not repeat the
  breadcrumb. The footer shows the selected name and size/folder type on the
  left and the key hints on the right.

## Fixtures and harness

- `tests/fixtures/home-columns/` is a runnable repository with mixed Markdown
  (headings, tables, lists, links, quotes, code), a long ASCII name, a long
  Chinese name, two nested folders, and `README.md`.
- `tests/validate_home_columns.ps1` copies the fixture to `out/home-columns/`,
  adds a PNG row and 24 generated notes so scrolling is exercised, then drives
  the browser with `WM_KEYDOWN` and captures `PrintWindow` images.

```powershell
pwsh -File tests/validate_home_columns.ps1
```

The final run used the normal `build/Release/tinta.exe`. If an older Tinta
instance is still running, close it before rebuilding (Windows keeps the
`.exe` locked otherwise).

## Checked

- 2400×1300 outer window (150% display): one column at the 900 DIP cap, two
  columns at 520 DIP each, three columns at 400 DIP each, group centred.
- 1440×900 outer window: one column at 800 DIP; long names still fit, and
  the footer selection plus all key hints are visible.
- 760×820 outer window: the single column fills the usable width, the footer
  keeps the selected item and drops the key hints, and long names ellipsize.
- Paper (light) and Midnight (dark) themes, including the selected-row bar,
  hover wash, badges, faint inactive borders and active accent border.
- Simplified Chinese: `根目录`, `29 项`, `projects · 2 项`, and the
  `文件夹` footer label render correctly next to untouched file names.
- Hover: a synthesized `WM_MOUSEMOVE` over a non-selected row produced the
  stronger hover band without disturbing the selected row.

Generated captures, all ignored by git:

- `out/home-columns/01-single-2400.png`
- `out/home-columns/02-two-2400.png`
- `out/home-columns/03-three-2400.png`
- `out/home-columns/04-single-file-2400.png`
- `out/home-columns/05-single-1440.png`
- `out/home-columns/06-hover-1440.png`
- `out/home-columns/07-single-760.png`
- `out/home-columns/08-dark-single-2400.png`
- `out/home-columns/09-dark-three-2400.png`
- `out/home-columns/10-zh-single-1440.png`
- `out/home-columns/11-zh-three-1440.png`

## Automated checks

`home_logic_tests` now locks the geometry rules:

- 1200 DIP window, one column → 800 DIP, centred.
- 1920 DIP window, one column → capped at 900 DIP.
- 1400 DIP window, two columns → 520 DIP each.
- 1600 DIP window, three columns → 400 DIP each.
- 700 DIP window, one column → fills 664 DIP.
- 900 DIP window, two/three columns → shrink to fit, never overflow.

Both `home_logic_tests` and `i18n_tests` pass.

## Remaining limitations

- Widths are validated in DIPs in the unit test; the screenshots run at 150%
  display scaling, so outer pixels are 1.5× the DIP values.
- Hover is simulated with a window message rather than a real pointer.
- Only Paper and Midnight were inspected visually; other themes use the same
  theme tokens and draw path.
- Three columns on a narrow window still get cramped (roughly 300 DIP at a
  960 DIP-wide window). The browser does not auto-collapse columns.
