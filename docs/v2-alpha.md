# omamd v2-alpha — Linux, macOS, iOS

Share one C core. Give each OS a native shell.

Linux keeps the GTK3 + WebKitGTK window. macOS and iOS use SwiftUI +
WKWebView. Markdown, palette, HTML wrapping, and the terminal pager stay
in C.

Tracking branch: `v2-alpha`. Work targets this branch until alpha is
ready to merge to `master`.

## Target tree

```
core/
  markdown.c / .h     # Markdown → HTML fragment
  theme.c / .h        # colors.toml → Palette
  html.c / .h         # Palette + fragment → full HTML page
  util.c / .h         # read file/stdin, path helpers
  term.c / .h         # ANSI pager (Linux + Mac CLI)
cli/
  main.c              # --html / --term / --theme  (no GTK)
linux/
  gtk.c               # today's window
apple/
  Omamd.xcodeproj     # SwiftUI + WKWebView, links core/*.c
fonts/ examples/ pkgbuild/ docs/
```

The C ABI is the product. Both shells call:

- `markdown_to_html()`
- `palette_load()` / `palette_load_file()`
- `omamd_document()` — fragment + palette + title → full HTML page

`--html` is a thin CLI over `omamd_document()`, so Terminal and the GUI
preview produce the same page.

## Already on this branch

Theme loading is out of GTK (`src/theme.c`). Lookup order:

1. `omamd --theme PATH`
2. `$OMAMD_THEME`
3. Omarchy live `~/.local/state/omarchy/current/theme/colors.toml`
4. `~/.config/omamd/colors.toml` (or `$XDG_CONFIG_HOME/omamd/colors.toml`)
5. Built-in dark palette

Paste a file at `~/.config/omamd/colors.toml` on a machine without
Omarchy. `examples/colors.toml` is the template.

## Still stuck in GTK (`src/main.c`)

| Function | Move to |
|---|---|
| Window, WebKitGTK, GFileMonitor, drag-drop, Hyprland chrome | `linux/gtk.c` |
| `apply_ui_css` / fontconfig registration | Linux only |

`make test` uses the CLI binary (`--html` / `--term`) with plain `cc`.
On Omarchy it also builds the GTK `omamd`.

## PRs (in order)

Each PR leaves the Omarchy GTK app working.

### PR 1 — HTML document in core — done on this branch

`src/html.c`: `omamd_css()` + `omamd_document()`. Fonts are a directory
argument; fontconfig stays in `main.c`.

### PR 2 — CLI without GTK — done on this branch

`src/cli.c` / `src/cli_main.c` / `src/util.c` / `src/fonts.c`.
`make` on a Mac produces `build/omamd` (`--html`, `--term`, pager).
On Omarchy, `build/omamd` is still GTK and `build/omamd-cli` is the
GTK-free binary. `make test` uses the CLI.

### PR 3 — Tree move

`src/` → `core/` + `linux/` + `cli/`. Update README, PKGBUILD,
`bin/build`. No behaviour change.

### PR 4 — Xcode skeleton (Mac first)

`apple/` SwiftUI app, macOS target. Bundle id `rocks.gurra.omamd`.
Bridging header includes `omamd.h` (markdown, theme, html, util).
One screen: WKWebView loading `omamd_document()` of
`examples/welcome.md`. Bundle the OFL fonts; register them with
Core Text. Viewer (`WindowGroup` + Open), not `DocumentGroup`.
Makefile `build/omamd` stays the CLI; the `.app` does not link
`cli.c`, `term.c`, or GTK.

### PR 5 — Mac viewer behaviour

Open/reload (`NSOpenPanel` + dispatch source on mtime). Preview / source
toggle. Palette from `~/.config/omamd/colors.toml`. Navigation: http(s)
→ Safari; relative `.md` → load in-app. Normal Mac titlebar. Hyprland
undecorated chrome stays in `linux/gtk.c`.

### PR 6 — iOS target

Same SwiftUI views, second destination. Document picker / Files.
Palette: bundled `examples/colors.toml`, plus import. No `--term`.
Share sheet for external links.

### PR 7 — CI

Linux: `make test` + GTK build. macOS: `make test` (CLI) + `xcodebuild`
for the Mac app and the iOS simulator.

## Rules

- Core `.c` files include only libc and other core headers. No GTK, no
  Foundation.
- Swift does not reimplement Markdown or TOML. Preview bugs go in
  `core/html.c` so Linux gets the same fix.
- Theme lookup stays in `theme.c`. Apple passes a path into
  `palette_load_file()`. Omarchy live-follow stays a Linux watcher
  around `theme_omarchy_live_dir()`.
- Fonts stay OFL-bundled. Each shell registers them (fontconfig /
  `@font-face` / `CTFontManager`).
- Apple bundle identifier is `rocks.gurra.omamd` on macOS and iOS.

## Platform contracts

| | Linux | macOS | iOS |
|---|---|---|---|
| Shell | GTK3 | SwiftUI | SwiftUI |
| Preview | WebKitGTK | WKWebView | WKWebView |
| Open file | path / drag-drop | NSOpenPanel | document picker |
| Palette | Omarchy live, then config | `~/.config/omamd/colors.toml` | bundled + import |
| `--html` / `--term` | yes | yes (CLI) | no |
| Window chrome | undecorated (Hyprland) | system titlebar | system chrome |
| Bundle id | — | `rocks.gurra.omamd` | `rocks.gurra.omamd` |

## Test on this Mac (after PR 2)

```
make test
```

Until then, the theme loader:

```
cc -std=c11 -Wall -Wextra -I src -o /tmp/omamd-theme-probe \
  -x c - src/theme.c <<'EOF'
#include "theme.h"
#include <stdio.h>
int main(int argc, char **argv) {
    Palette p;
    char path[512];
    const char *cli = argc > 1 ? argv[1] : NULL;
    ThemeKind k = theme_resolve(cli, path, sizeof(path), 1);
    palette_load(&p, cli);
    printf("kind=%d path=%s bg=%s fg=%s\n", (int)k, path[0]?path:"-", p.bg, p.fg);
    return 0;
}
EOF
/tmp/omamd-theme-probe examples/colors.toml
```
