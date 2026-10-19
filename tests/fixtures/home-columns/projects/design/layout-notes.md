# Layout notes

The column group is centred. A single column is allowed to grow to 800–900
DIP; two columns settle at about 520 DIP and three at about 400 DIP.

## Rhythm

- Row height: 34 DIP.
- Horizontal row padding: 18 DIP.
- Gap between columns: 16 DIP.

```css
.column {
    width: min(900px, max(800px, 58vw));
    border-radius: 9px;
}
```

> On a narrow window the single column fills the usable width edge to edge.
