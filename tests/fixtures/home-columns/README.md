# Home columns fixture

This repository drives the Home B Miller-column browser. It is ordinary
Markdown on purpose: the file manager has to show real names, real types and
real metadata while the document itself still opens and renders.

## What the browser should show

| Element | Expected behaviour |
| --- | --- |
| Single column | 800 DIP wide (900 on very wide windows), centred |
| Two columns | about 520 DIP each, group centred |
| Three columns | about 400 DIP each, group centred |
| Narrow window | the column fills the available width |
| Folder row | accent chevron and folder name |
| File row | MD/IMG badge, name, size and modified time |
| Selected row | accent wash plus a 2 px leading bar |

## Mixed content check

- A list item with **strong text**, *emphasis* and `inline code`.
- A [link to the design notes](projects/design/layout-notes.md).
- A parent folder whose first child is another folder, so pressing Enter
  twice reaches three columns.

> Quotes and code should survive next to the browser chrome.

```cpp
int columns(float width) {
    return width > 1100.0f ? 3 : width > 720.0f ? 2 : 1;
}
```

The footer should read `README.md · <size>` on the left and the key hints on
the right.
