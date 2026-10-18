# Preview follow regression

The docked preview should follow the caret while this document is edited.
This fixture mixes paragraphs, lists, tables, quotes, code and math so the
scroll anchors see more than one kind of block.

## Opening paragraph

Tinta keeps the rendered page beside the source. Editing near the top of a
long document should not move the preview when the caret already sits in
the comfort band. Moving the caret further down should nudge the page just
enough to keep the edited line in view.

The quick brown fox jumps over the lazy dog. The quick brown fox jumps over
the lazy dog. The quick brown fox jumps over the lazy dog.

- first list item with a short line
- second list item with **bold** and `inline code`
- third list item that is deliberately a little longer than the others

> A blockquote should carry its own scroll anchor.
>
> It has two paragraphs so the block is not a single line.

## A table

| Name | Value | Notes |
| :--- | ---: | :--- |
| alpha | 1 | first row |
| bravo | 2 | second row |
| charlie | 3 | third row |
| delta | 4 | fourth row |
| echo | 5 | fifth row |
| foxtrot | 6 | sixth row |
| golf | 7 | seventh row |
| hotel | 8 | eighth row |

## A code block

```cpp
int add(int a, int b) {
    return a + b;
}

int main() {
    int total = 0;
    for (int i = 0; i < 10; i++) {
        total = add(total, i);
    }
    return total;
}
```

## Middle paragraph

Typing in the middle of the document is the common case. The preview should
stay still while the caret remains between roughly a fifth and two thirds of
the pane, then nudge by a small amount when the caret crosses the lower
edge.

The quick brown fox jumps over the lazy dog. The quick brown fox jumps over
the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown
fox jumps over the lazy dog.

## Math and inline formatting

An inline equation $a^2 + b^2 = c^2$ sits in this paragraph, followed by a
display equation:

$$
\frac{1}{1 + x^2}
$$

## Closing paragraph

The final blocks exist so the document is taller than the preview pane at
the test window size. The last line should be reachable, and a caret at the
very end should clamp at the bottom rather than scrolling past the content.

The quick brown fox jumps over the lazy dog. The quick brown fox jumps over
the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown
fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog.

nihao shijie 


nihao 
niah


nihao
nihao




