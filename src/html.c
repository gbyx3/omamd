#define _DEFAULT_SOURCE

/*
 * html.c — turn a Palette and an HTML fragment into a page.
 *
 * The growable buffer is the same idea as Buf in markdown.c, copied
 * here so this file does not include GTK/GLib and does not share
 * markdown.c's internals.
 */

#include "html.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int oom;
} Buf;

static void buf_init(Buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->oom = 0;
}

static int buf_reserve(Buf *b, size_t extra)
{
    size_t need;
    size_t new_cap;
    char *grown;

    if (b->oom)
        return 0;
    if (extra > (size_t)-1 - b->len) {
        b->oom = 1;
        return 0;
    }
    need = b->len + extra;
    if (need <= b->cap)
        return 1;
    new_cap = b->cap ? b->cap : 64;
    while (new_cap < need) {
        if (new_cap > (size_t)-1 / 2) {
            new_cap = (size_t)-1;
            break;
        }
        new_cap *= 2;
    }
    grown = realloc(b->data, new_cap);
    if (!grown) {
        b->oom = 1;
        return 0;
    }
    b->data = grown;
    b->cap = new_cap;
    return 1;
}

static void buf_put(Buf *b, const char *s, size_t n)
{
    if (!buf_reserve(b, n))
        return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void buf_puts(Buf *b, const char *s)
{
    if (!s)
        return;
    buf_put(b, s, strlen(s));
}

static void buf_putc(Buf *b, char c)
{
    if (!buf_reserve(b, 1))
        return;
    b->data[b->len++] = c;
}

static void buf_escape(Buf *b, const char *s)
{
    for (; s && *s; s++) {
        switch (*s) {
        case '&':  buf_puts(b, "&amp;");  break;
        case '<':  buf_puts(b, "&lt;");   break;
        case '>':  buf_puts(b, "&gt;");   break;
        case '"':  buf_puts(b, "&quot;"); break;
        default:   buf_putc(b, *s);       break;
        }
    }
}

static char *buf_take(Buf *b)
{
    char *s;

    if (b->oom) {
        free(b->data);
        b->data = NULL;
        b->len = 0;
        b->cap = 0;
        b->oom = 0;
        return NULL;
    }
    if (!buf_reserve(b, 1)) {
        free(b->data);
        b->data = NULL;
        b->len = 0;
        b->cap = 0;
        return NULL;
    }
    b->data[b->len] = '\0';
    s = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return s;
}

/* file:///absolute/path with RFC 3986 percent-encoding, so a space
 * in the font directory does not break the CSS url(). */
static void buf_file_uri(Buf *b, const char *path)
{
    const unsigned char *p;

    buf_puts(b, "file://");
    if (!path || !path[0])
        return;
    if (path[0] != '/')
        buf_putc(b, '/');
    for (p = (const unsigned char *)path; *p; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '/' || c == '-' || c == '.' || c == '_' || c == '~') {
            buf_putc(b, (char)c);
        } else {
            char tmp[4];
            snprintf(tmp, sizeof(tmp), "%%%02X", c);
            buf_puts(b, tmp);
        }
    }
}

static int font_file_ok(const char *dir, const char *file)
{
    char path[4400];
    struct stat st;

    if (!dir || !dir[0] || !file)
        return 0;
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    if (stat(path, &st) != 0)
        return 0;
    return S_ISREG(st.st_mode);
}

static void append_font_faces(Buf *b, const char *font_dir)
{
    static const struct {
        const char *file;
        const char *style;
        const char *weight;
    } faces[] = {
        { "iAWriterMonoS-Regular.ttf", "normal", "400" },
        { "iAWriterMonoS-Italic.ttf", "italic", "400" },
        { "iAWriterMonoS-Bold.ttf", "normal", "700" },
        { "iAWriterMonoS-BoldItalic.ttf", "italic", "700" },
    };
    size_t i;
    char path[4400];
    char real[PATH_MAX];
    const char *dir = font_dir;

    if (!dir || !dir[0])
        return;
    if (realpath(dir, real))
        dir = real;
    if (!font_file_ok(dir, "iAWriterMonoS-Regular.ttf"))
        return;
    for (i = 0; i < sizeof(faces) / sizeof(faces[0]); i++) {
        if (!font_file_ok(dir, faces[i].file))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, faces[i].file);
        buf_puts(b, "@font-face{font-family:\"iA Writer Mono S\";font-style:");
        buf_puts(b, faces[i].style);
        buf_puts(b, ";font-weight:");
        buf_puts(b, faces[i].weight);
        buf_puts(b, ";src:url('");
        buf_file_uri(b, path);
        buf_puts(b, "') format('truetype');font-display:swap;}");
    }
}

char *omamd_css(const Palette *p)
{
    Palette def;
    Buf b;

    if (!p) {
        palette_default(&def);
        p = &def;
    }
    buf_init(&b);
    buf_puts(&b,
        ":root {\n"
        "  --bg: ");
    buf_puts(&b, p->bg);
    buf_puts(&b, ";\n  --fg: ");
    buf_puts(&b, p->fg);
    buf_puts(&b, ";\n  --muted: ");
    buf_puts(&b, p->muted);
    buf_puts(&b, ";\n  --accent: ");
    buf_puts(&b, p->accent);
    buf_puts(&b, ";\n  --code-bg: ");
    buf_puts(&b, p->code_bg);
    buf_puts(&b, ";\n  --surface: ");
    buf_puts(&b, p->surface);
    buf_puts(&b, ";\n  --sel: ");
    buf_puts(&b, p->sel);
    buf_puts(&b,
        ";\n"
        "}\n"
        "html, body {\n"
        "  background: var(--bg);\n"
        "  color: var(--fg);\n"
        "  margin: 0;\n"
        "}\n"
        "body {\n"
        "  font-family: \"iA Writer Mono S\", ui-monospace, monospace;\n"
        "  font-size: 16px;\n"
        "  line-height: 1.7;\n"
        "}\n"
        "article.md {\n"
        "  max-width: 42rem;\n"
        "  margin: 0 auto;\n"
        "  padding: 2.4rem 4.2rem 4rem 1.4rem;\n"
        "}\n"
        "h1, h2, h3, h4, h5, h6 {\n"
        "  line-height: 1.25;\n"
        "  font-weight: 700;\n"
        "  margin: 1.6em 0 0.5em;\n"
        "}\n"
        "h1 { font-size: 2.0em; margin-top: 0; }\n"
        "h2 { font-size: 1.45em; padding-bottom: 0.2em;\n"
        "     border-bottom: 1px solid var(--surface); }\n"
        "h3 { font-size: 1.18em; }\n"
        "p, ul, ol, blockquote, table, pre { margin: 0.85em 0; }\n"
        "a { color: var(--accent); text-decoration: none; }\n"
        "a:hover { text-decoration: underline; }\n"
        "code {\n"
        "  font-family: \"iA Writer Mono S\", ui-monospace, monospace;\n"
        "  font-size: 0.92em;\n"
        "  background: var(--code-bg);\n"
        "  padding: 0.12em 0.38em;\n"
        "  border-radius: 4px;\n"
        "}\n"
        "pre {\n"
        "  background: var(--code-bg);\n"
        "  border: 1px solid var(--surface);\n"
        "  border-radius: 8px;\n"
        "  padding: 0.9em 1em;\n"
        "  overflow: auto;\n"
        "}\n"
        "pre code { background: none; padding: 0; font-size: 0.86em; }\n"
        "blockquote {\n"
        "  border-left: 3px solid var(--accent);\n"
        "  margin-left: 0;\n"
        "  padding: 0.15em 0 0.15em 1em;\n"
        "  color: var(--muted);\n"
        "}\n"
        "hr {\n"
        "  border: 0;\n"
        "  border-top: 1px solid var(--surface);\n"
        "  margin: 1.8em 0;\n"
        "}\n"
        "table { border-collapse: collapse; width: 100%; }\n"
        "th, td {\n"
        "  border: 1px solid var(--surface);\n"
        "  padding: 0.4em 0.7em;\n"
        "  text-align: left;\n"
        "}\n"
        "th { background: var(--code-bg); }\n"
        "tr:nth-child(even) td { background: color-mix(in srgb, var(--surface) 35%, transparent); }\n"
        "img { max-width: 100%; height: auto; border-radius: 6px; }\n"
        "li > p { margin: 0.25em 0; }\n"
        "li:has(> input[type=checkbox]) { list-style: none; margin-left: -1.3em; }\n"
        "input[type=checkbox] { margin-right: 0.45em; }\n"
        "::selection { background: var(--sel); }\n");
    return buf_take(&b);
}

char *omamd_document(const char *title, const char *css, const char *body,
                     const char *font_dir)
{
    Buf b;

    buf_init(&b);
    buf_puts(&b,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>");
    buf_escape(&b, title ? title : "omamd");
    buf_puts(&b, "</title><style>");
    append_font_faces(&b, font_dir);
    if (css)
        buf_puts(&b, css);
    buf_puts(&b, "</style></head><body><article class=\"md\">");
    if (body)
        buf_puts(&b, body);
    buf_puts(&b, "</article><div id=\"omamd-end\"></div></body></html>");
    return buf_take(&b);
}
