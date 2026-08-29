# Welcome to omamd

This file is a tour of what the viewer can render. Open it with:

```
omamd examples/welcome.md
```

The round button in the top-right corner (or `Ctrl+1` / `Ctrl+2`) switches between the rendered page and the Markdown source. It stays put when you scroll.

## Emphasis

Regular text, *italic*, **bold**, ***both***, ~~struck through~~, and `inline code`.

Escapes work too: \*this is not italic\*.

## Lists

- Unordered item
- Another item
  - Nested under it
- [ ] A task still open
- [x] A task already done

1. Ordered first
2. Ordered second
3. Ordered third

## Quote, rule, link

> Markdown is just text with a few markers.
> The parser turns those markers into HTML tags.

---

A [link to the Markdown guide](https://www.markdownguide.org) opens in your browser. A relative `.md` link opens in omamd.

Autolink: https://omarchy.org and <https://github.com/mity/md4c>.

## Table

| Syntax | Meaning |
| ------ | ------- |
| `#`    | heading |
| `**`   | bold    |
| `` ` `` | code   |

Alignment:

| Left | Center | Right |
| :--- | :----: | ----: |
| a    | b      | c     |

## Code

```c
char *html = markdown_to_html(md, n);
/* caller must free(html) */
```

Indented code:

    fopen, fread, fclose
    malloc, realloc, free

## Heading styles

Setext H1
=========

Setext H2
---------

### ATX h3

#### ATX h4
