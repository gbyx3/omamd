#define _DEFAULT_SOURCE

/*
 * cli.c — --html, --term, and the flags that both binaries share.
 */

#include "cli.h"

#include "fonts.h"
#include "html.h"
#include "markdown.h"
#include "term.h"
#include "theme.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void omamd_cli_usage(FILE *out)
{
    fprintf(out,
            "omamd %s — a small Markdown viewer\n"
            "\n"
            "Usage:\n"
            "  omamd [file.md]         open in a window (or the pager)\n"
            "  omamd --term [file.md]  render in the terminal (SSH)\n"
            "  omamd --html [file.md]  Markdown to HTML on stdout\n"
            "  omamd --theme FILE.toml  use this colors.toml\n"
            "  omamd --help            this text\n"
            "\n"
            "Keys:  Ctrl+O open   Ctrl+R reload   Ctrl+1 preview\n"
            "       Ctrl+2 source Ctrl+Q quit     F5 reload\n"
            "Term:  j/k scroll    g/G top/end     f follow  q quit\n",
            OMAMD_VERSION);
}

int omamd_cli_parse(int argc, char **argv, OmamdCli *o)
{
    int i;

    memset(o, 0, sizeof(*o));
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            o->help = 1;
            continue;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            o->version = 1;
            continue;
        }
        if (strcmp(argv[i], "--html") == 0) {
            o->html = 1;
            continue;
        }
        if (strcmp(argv[i], "--term") == 0 || strcmp(argv[i], "-t") == 0) {
            o->term = 1;
            continue;
        }
        if (strcmp(argv[i], "--theme") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                fprintf(stderr, "omamd: --theme needs a file path\n");
                o->err = 2;
                return 2;
            }
            o->theme = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "omamd: unknown option %s\n", argv[i]);
            o->err = 2;
            return 2;
        }
        o->path = argv[i];
    }
    return 0;
}

int omamd_run_html(const char *path, const char *theme)
{
    size_t n = 0;
    char *md;
    char *fragment;
    char *css;
    char *page;
    Palette pal;
    const char *title = "omamd";
    const char *fonts;

    if (path) {
        const char *slash;
        md = omamd_read_file(path, &n);
        if (!md) {
            fprintf(stderr, "omamd: cannot read %s\n", path);
            return 1;
        }
        slash = strrchr(path, '/');
        title = slash ? slash + 1 : path;
    } else {
        md = omamd_read_stdin(&n);
        if (!md) {
            fprintf(stderr, "omamd: out of memory\n");
            return 1;
        }
    }

    fragment = markdown_to_html(md, n);
    free(md);
    if (!fragment) {
        fprintf(stderr, "omamd: out of memory\n");
        return 1;
    }
    palette_load(&pal, theme);
    css = omamd_css(&pal);
    fonts = omamd_font_dir();
    page = omamd_document(title, css, fragment, fonts[0] ? fonts : NULL);
    free(fragment);
    free(css);
    if (!page) {
        fprintf(stderr, "omamd: out of memory\n");
        return 1;
    }
    fputs(page, stdout);
    free(page);
    return 0;
}

static unsigned rgb_parse(const char *s)
{
    unsigned r = 0xcd, g = 0xd6, b = 0xf4;
    if (!s || s[0] != '#')
        return (r << 16) | (g << 8) | b;
    if (s[1] && s[2] && s[3] && s[4] == '\0') {
        if (sscanf(s, "#%1x%1x%1x", &r, &g, &b) == 3)
            return (r * 17u << 16) | (g * 17u << 8) | (b * 17u);
    }
    if (sscanf(s, "#%02x%02x%02x", &r, &g, &b) == 3)
        return (r << 16) | (g << 8) | b;
    return (0xcdu << 16) | (0xd6u << 8) | 0xf4u;
}

static char *term_reread(const char *path, size_t *n)
{
    return omamd_read_file(path, n);
}

int omamd_run_term(const char *path, const char *theme)
{
    size_t n = 0;
    char *md;
    Palette pal;
    TermPalette tp;
    int rc;
    const char *watch = path;

    if (path) {
        md = omamd_read_file(path, &n);
        if (!md) {
            fprintf(stderr, "omamd: cannot read %s\n", path);
            return 1;
        }
    } else {
        md = omamd_read_stdin(&n);
        if (!md) {
            fprintf(stderr, "omamd: out of memory\n");
            return 1;
        }
        watch = NULL;
    }

    palette_load(&pal, theme);
    memset(&tp, 0, sizeof(tp));
    tp.color = getenv("NO_COLOR") == NULL;
    tp.fg = rgb_parse(pal.fg);
    tp.bg = rgb_parse(pal.bg);
    tp.accent = rgb_parse(pal.accent);
    tp.muted = rgb_parse(pal.muted);
    tp.code_bg = rgb_parse(pal.code_bg);

    rc = term_run(watch, md, n, &tp, watch ? term_reread : NULL);
    free(md);
    return rc;
}

int omamd_no_display(void)
{
    const char *w = getenv("WAYLAND_DISPLAY");
    const char *d = getenv("DISPLAY");
    return (w == NULL || w[0] == '\0') && (d == NULL || d[0] == '\0');
}
