# omamd

A Markdown viewer for files that come out of AI work: chat exports, generated docs, READMEs, notes dumped to `.md`. Open them in a window that follows your Omarchy theme, or convert them to HTML.

```
make
./omamd examples/welcome.md
```

Needs `gtk3` and `webkit2gtk-4.1` (already on Omarchy).

## Viewer

```
omamd notes.md
```

Bare window, no title bar. The round button in the top-right switches Preview and Source and stays put while you scroll. If the file changes on disk — an AI run appending to it, or a save from another editor — omamd reloads and eases down to the new bottom. Drop a `.md` on the window to open it. Relative links to other Markdown files in the same folder open in omamd; `http`/`https` links go to the browser.

The preview uses the current Omarchy palette (`~/.local/state/omarchy/current/theme/colors.toml`) and updates when you `omarchy theme set`.

| Key | Action |
|-----|--------|
| `Ctrl+O` | Open |
| `Ctrl+R` / `F5` | Reload |
| `Ctrl+1` / `Ctrl+2` | Preview / Source |
| `Ctrl+Q` | Quit |

## Markdown to HTML converter

```
omamd --html notes.md > notes.html
omamd --html < notes.md > notes.html
```

Turns Markdown into a full HTML document (styled with the current Omarchy colours) on stdout. No window. Useful when a pipeline already has `.md` and you want a page you can archive or attach.

Unsafe URL schemes (`javascript:`, `file:`, `data:text/html`, rooted `/etc/...` paths) are stripped so the HTML is fit to open in a browser. See `ISSUES.md`.

## Markdown it understands

Headings, **bold**, *italic*, ~~strike~~, `code`, fenced and indented blocks, lists, task boxes, quotes, tables, links, images, autolinks. Everyday README Markdown, not every corner of CommonMark.

## Reading the code

Written in C, commented for someone new to the language.

1. `src/markdown.h` — the converter’s public function
2. `src/markdown.c` — Markdown → HTML
3. `src/main.c` — window, file loading, Omarchy colours
4. `Makefile` — how `gcc` is invoked
