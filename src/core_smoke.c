/*
 * core_smoke.c — Markdown + theme + html with no GTK.
 *
 * Used by `make test-core` so this Mac can check the parser and the
 * page wrapper without WebKitGTK.  PR 2 will fold this into cli/.
 */

#include "html.h"
#include "markdown.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OMAMD_MAX_FILE_BYTES (32u * 1024u * 1024u)

static char *read_entire_file(const char *path, size_t *out_len)
{
    FILE *f;
    long size;
    char *buf;
    size_t got;

    f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0 || (unsigned long)size > OMAMD_MAX_FILE_BYTES) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len)
        *out_len = got;
    return buf;
}

int main(int argc, char **argv)
{
    const char *md_path = NULL;
    const char *theme = NULL;
    const char *font_dir = "fonts";
    const char *title;
    const char *slash;
    char *md;
    char *fragment;
    char *css;
    char *page;
    Palette pal;
    size_t n = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--theme") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "omamd-html: --theme needs a file path\n");
                return 2;
            }
            theme = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "omamd-html: unknown option %s\n", argv[i]);
            return 2;
        }
        md_path = argv[i];
    }
    if (!md_path) {
        fprintf(stderr, "usage: omamd-html [--theme FILE.toml] file.md\n");
        return 2;
    }

    md = read_entire_file(md_path, &n);
    if (!md) {
        fprintf(stderr, "omamd-html: cannot read %s\n", md_path);
        return 1;
    }
    fragment = markdown_to_html(md, n);
    free(md);
    if (!fragment) {
        fprintf(stderr, "omamd-html: out of memory\n");
        return 1;
    }

    palette_load(&pal, theme);
    css = omamd_css(&pal);
    if (!css) {
        free(fragment);
        fprintf(stderr, "omamd-html: out of memory\n");
        return 1;
    }

    slash = strrchr(md_path, '/');
    title = slash ? slash + 1 : md_path;
    page = omamd_document(title, css, fragment, font_dir);
    free(fragment);
    free(css);
    if (!page) {
        fprintf(stderr, "omamd-html: out of memory\n");
        return 1;
    }
    fputs(page, stdout);
    free(page);
    return 0;
}
