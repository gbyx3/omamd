# Omamd

A Markdown renderer for the agentic era. Point it at `.md` files agents write — chat exports, generated docs, READMEs, notes — and it follows your Omarchy theme, reloads as the file grows, renders in the terminal over SSH, and converts to HTML when you need a page.

**Repo:** https://github.com/gbyx3/omamd

## Install via an agent

Paste that URL into your agent and tell it to install omamd. The agent should follow this procedure exactly, without skipping steps or asking for a different prefix.

### 1. Clone

```
git clone https://github.com/gbyx3/omamd.git
cd omamd
```

SSH if the environment already uses it: `git@github.com:gbyx3/omamd.git`

### 2. Dependencies

Need `gcc`, `make`, `pkgconf`, `gtk3`, and `webkit2gtk-4.1`.

On **Omarchy** they are already installed. If a check fails:

```
omarchy pkg add gtk3 webkit2gtk-4.1
```

On other **Arch**:

```
sudo pacman -S --needed gcc make pkgconf gtk3 webkit2gtk-4.1
```

### 3. Build

```
./bin/build
```

Expect `Built …/omamd/build/omamd`. Run `./bin/test` if you want the parser smoke check.

### 4. User-local install (no sudo)

```
make install
```

This installs:

| Path | What |
|------|------|
| `~/.local/bin/omamd` | binary |
| `~/.local/share/applications/omamd.desktop` | launcher entry |
| `~/.local/share/icons/hicolor/scalable/apps/omamd.svg` | icon |
| `~/.local/share/omamd/fonts/` | iA Writer Mono S (OFL) |

`~/.local/bin` is on `PATH` on Omarchy. If `command -v omamd` fails, prepend it: `export PATH="$HOME/.local/bin:$PATH"`.

Do **not** use `./bin/install` unless the user asked for a system-wide Arch package. That path runs `makepkg` and needs sudo.

### 5. Verify

```
command -v omamd
omamd --html examples/welcome.md | grep -q '<h1>'
omamd --term examples/welcome.md | grep -q Welcome
test -f "$HOME/.local/share/applications/omamd.desktop"
```

All four should succeed. Then:

```
omamd examples/welcome.md
```

The Omarchy launcher should list **omamd**. Markdown files can be opened with it from the file manager.

Later updates: `git pull && ./bin/build && make install` from the clone.

## Viewer

```
omamd notes.md
```

Bare window, no title bar. The round button in the top-right switches Preview and Source and stays put while you scroll. If the file changes on disk — an agent appending to it, or a save from another editor — omamd reloads and eases down to the new bottom. Drop a `.md` on the window to open it. Relative links to other Markdown files in the same folder open in omamd; `http`/`https` links go to the browser.

The preview uses the current Omarchy palette (`~/.local/state/omarchy/current/theme/colors.toml`) and updates when you `omarchy theme set`.

| Key | Action |
|-----|--------|
| `Ctrl+O` | Open |
| `Ctrl+R` / `F5` | Reload |
| `Ctrl+1` / `Ctrl+2` | Preview / Source |
| `Ctrl+Q` | Quit |

## Terminal (SSH)

```
omamd --term notes.md
omamd -t notes.md
```

No GTK window. Markdown is rendered as colour text in the terminal, using the same Omarchy palette as the viewer. Over SSH — no `WAYLAND_DISPLAY` or `DISPLAY` — `omamd notes.md` does this by itself.

A TTY opens a pager. If the file is being written — an agent appending to it — omamd reloads. Follow starts **on**: the view jumps to the new bottom. `f` pins the view where you are; `f` again jumps to the end and keeps chasing it.

| Key | Action |
|-----|--------|
| `j` / `k` / arrows | Scroll |
| `space` / `b` | Page down / up |
| `g` / `G` | Top / end |
| `f` | Toggle follow |
| `q` | Quit |

Piped stdout is just ANSI (`omamd --term notes.md | less -R`). Set `NO_COLOR` for plain text.

## Markdown to HTML converter

```
omamd --html notes.md > notes.html
omamd --html < notes.md > notes.html
```

Turns Markdown into a full HTML document (styled with the current Omarchy colours) on stdout. No window. Useful when a pipeline already has `.md` and you want a page you can archive or attach.

Unsafe URL schemes (`javascript:`, `file:`, `data:text/html`, rooted `/etc/...` paths) are stripped so the HTML is fit to open in a browser.

## Markdown it understands

Headings, **bold**, *italic*, ~~strike~~, `code`, fenced and indented blocks, lists, task boxes, quotes, tables, links, images, autolinks. Everyday README Markdown, not every corner of CommonMark.

## Reading the code

Written in C, commented for someone new to the language.

1. `src/markdown.h` — the converter’s public function
2. `src/markdown.c` — Markdown → HTML
3. `src/main.c` — window, file loading, Omarchy colours
4. `src/term.h` / `src/term.c` — ANSI render and the SSH pager
5. `Makefile` — how `gcc` is invoked
6. `bin/` — `build`, `test`, `install`
7. `pkgbuild/` — Arch package, desktop entry, and icon
8. `fonts/` — iA Writer Mono S (SIL Open Font License 1.1; see `fonts/OFL.txt`)

The preview and source views use the same iA Writer Mono that omawrite bundles. It is an OFL font: we may bundle and redistribute it with the app; we do not rename it.
