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
  omamd.h             # umbrella ABI for Swift
  markdown.c / .h     # Markdown → HTML fragment
  theme.c / .h        # colors.toml → Palette
  html.c / .h         # Palette + fragment → full HTML page
  util.c / .h         # read file/stdin, path helpers
  fonts.c / .h        # bundled iA Writer Mono S search
  term.c / .h         # ANSI pager (Linux + Mac CLI)
cli/
  cli.c / .h          # argv, --html, --term
  main.c              # GTK-free entry
linux/
  gtk.c               # GTK window
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

Theme loading is out of GTK (`core/theme.c`). Lookup order:

1. `omamd --theme PATH`
2. `$OMAMD_THEME`
3. Omarchy live `~/.local/state/omarchy/current/theme/colors.toml`
4. `~/.config/omamd/colors.toml` (or `$XDG_CONFIG_HOME/omamd/colors.toml`)
5. Built-in dark palette

Paste a file at `~/.config/omamd/colors.toml` on a machine without
Omarchy. `examples/colors.toml` is the template.

## Linux GTK (`linux/gtk.c`)

Window, WebKitGTK, GFileMonitor, drag-drop, Hyprland chrome, and
fontconfig registration stay here.

The preview/source toggle stays top-right. Open, Theme, and Follow sit
in a bottom-right overlay cluster, matching the Apple chrome. Follow is
a pin (on by default) and is stored in `~/.config/omamd/ui.ini`. Theme
pops a menu: Omarchy (live), Default (built-in dark), bundled
`examples/themes/*.toml`, and Choose File. A picked palette is a GTK
pin so it wins over the Omarchy live file without changing `theme.c`
lookup for `--html` / `--term`.

`make test` uses the CLI binary (`--html` / `--term`) with plain `cc`.
On Omarchy it also builds the GTK `omamd`.

## PRs (in order)

Each PR leaves the Omarchy GTK app working.

### PR 1 — HTML document in core — done on this branch

`core/html.c`: `omamd_css()` + `omamd_document()`. Fonts are a directory
argument; fontconfig stays in `linux/gtk.c`.

### PR 2 — CLI without GTK — done on this branch

`cli/cli.c` / `cli/main.c` / `core/util.c` / `core/fonts.c`.
`make` on a Mac produces `build/omamd` (`--html`, `--term`, pager).
On Omarchy, `build/omamd` is still GTK and `build/omamd-cli` is the
GTK-free binary. `make test` uses the CLI.

### PR 3 — Tree move — done on this branch

`src/` → `core/` + `linux/` + `cli/`. `core/omamd.h` is the umbrella
header for Swift. Makefile uses `-I core -I cli`. No behaviour change.

### PR 4 — Xcode skeleton (Mac first) — done on this branch

`apple/` SwiftUI app, macOS target. Bundle id `rocks.gurra.omamd`.
Bridging header includes `omamd.h`. One screen: WKWebView loading
`omamd_document()` of bundled `examples/welcome.md`. OFL fonts are
copied into the app bundle and registered with Core Text.
`Omamd.page(...)` is the Swift overlay; views do not call C pointers.
Viewer (`WindowGroup`), not `DocumentGroup`. The `.app` does not
link `cli.c`, `term.c`, or GTK. `make test-mac` runs `xcodebuild`.

### PR 5 — Mac viewer behaviour — done on this branch

Open/reload (`NSOpenPanel` + dispatch source on mtime). Preview / source
toggle (overlay button, `⌘1` / `⌘2`). Palette from
`~/.config/omamd/colors.toml`. Navigation: http(s) → Safari; relative
`.md` → load in-app. Normal Mac titlebar. Hyprland undecorated chrome
stays in `linux/gtk.c`. Yabai tab-jump is held (#7). Mac Settings (`⌘,`) is a grouped pane: theme, window opacity,
hide title bar, Follow. View → Theme still copies a bundled
`examples/themes/*.toml` (Omarchy quattro + Paper) to
`~/.config/omamd/colors.toml`. Hide title bar (Ghostty-style, traffic
lights stay) is on by default.

### PR 6 — iOS target — started on this branch

Same SwiftUI views, second destination (`iphoneos` / `iphonesimulator`,
iOS 17). Document picker / Files. Palette: bundled themes copied into
Application Support. No `--term`. http(s) opens in Safari. `make test-ios`
builds for the iPhone 17 simulator. Device install still needs a
signing team.

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
| Open file | path / drag-drop / overlay | NSOpenPanel | Files picker |
| Palette | Omarchy live, then config; overlay can pin | `~/.config/omamd/colors.toml` | bundled + import |
| `--html` / `--term` | yes | yes (CLI) | no |
| Window chrome | undecorated (Hyprland) | system titlebar | system chrome |
| Bundle id | — | `rocks.gurra.omamd` | `rocks.gurra.omamd` |

## Test on this Mac

```
make test
make test-mac
```
