#define _DEFAULT_SOURCE

/*
 * main.c — the window, the buttons, and the file loading.
 *
 * Read markdown.c first.  That file is the language lesson.
 * This file is the "how a graphical C program is wired" lesson.
 *
 * GTK (the GIMP Toolkit) is a C library.  Every on-screen thing is
 * a *widget*: a window, a button, a text view, a WebKit page.  You
 * create widgets, put them inside other widgets (a stack of pages
 * inside an overlay, a pair of buttons on top of that overlay),
 * and connect *signals* (events like "clicked" or "destroy") to
 * functions you write.
 *
 * The life of this program:
 *
 *   1. Look at the command-line arguments.
 *   2. If the user asked for --html, convert and print, then exit.
 *      No window.  This is how we test the parser from a terminal.
 *   3. Otherwise gtk_init() talks to the Wayland/X11 display.
 *   4. Build the window, connect signals, load a file if one was given.
 *   5. gtk_main() sits in a loop: wait for an event, handle it, repeat
 *      until the window is closed.
 *
 * Why GTK and not Qt, on Omarchy:
 *   Omarchy themes GTK natively.  Qt apps are told to *imitate* GTK
 *   (the environment variable QT_QPA_PLATFORMTHEME=gtk3).  Qt is also
 *   C++ — classes, a "moc" pre-processor, signals as language syntax.
 *   This project is C so the comments can stay about pointers, malloc,
 *   and structs.  GTK is the C toolkit that already looks like Omarchy.
 */

#include "markdown.h"

#include <ctype.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webkit2/webkit2.h>

/* A #define is a name the preprocessor pastes in before compiling.
 * OMAMD_VERSION is not a variable; it is literally the characters "0.1". */
#define OMAMD_VERSION "0.1"

/* Refuse to slurp a multi-gigabyte "markdown" file into RAM. */
#define OMAMD_MAX_FILE_BYTES (32u * 1024u * 1024u)

/* ---------------------------------------------------------------
 * The App struct: all the program's live state in one bundle.
 *
 * GTK callbacks only get a pointer (gpointer is GTK's name for void *).
 * We pass the address of an App so every callback can see the window,
 * the current file, and so on.  That is cleaner than global variables.
 * --------------------------------------------------------------- */

typedef struct {
    GtkWidget *window;
    GtkWidget *web_view;
    GtkWidget *text_view;
    GtkWidget *stack;
    GtkWidget *mode_btn;
    char icon_fg[16];      /* hex colour for the floating toggle icon */

    char *path;            /* malloc'd path of the open file, or NULL */
    char *doc_dir;         /* realpath of that file's directory, or NULL */
    char *base_uri;        /* file:// URI of that file's directory */
    char *source;          /* malloc'd Markdown text currently shown */
    char *css;             /* malloc'd stylesheet, from Omarchy if possible */
    GtkCssProvider *ui_css; /* GTK chrome stylesheet; we reload this in place */

    GFileMonitor *monitor; /* watches the open Markdown file */
    guint reload_timeout;  /* 0, or a pending "reload soon" timer id */
    GtkWidget *source_scroll;
    int follow_preview;    /* after a live file change, ease to the bottom */
    int follow_source;
    guint source_tick;
    guint follow_source_idle_id;
    gdouble scroll_from;
    gdouble scroll_to;
    gint64 scroll_t0;

    GFileMonitor *theme_dir_mon;    /* ~/.local/state/omarchy/current/ */
    GFileMonitor *theme_colors_mon; /* .../theme/colors.toml */
    guint theme_timeout;
} App;

/* ---------------------------------------------------------------
 * Tiny helpers: files and strings
 * ---------------------------------------------------------------
 *
 * fopen / fread / fclose is the classic C way to read a file.
 * GTK also has g_file_get_contents(); we use the C version here
 * so you can see the pattern, then free() the result.
 */

static char *read_entire_file(const char *path, size_t *out_len)
{
    FILE *f;
    long size;
    char *buf;
    size_t got;

    /* "rb" = read, binary.  Binary so Windows \r\n is not rewritten;
     * the parser already understands both kinds of line ending. */
    f = fopen(path, "rb");
    if (!f)
        return NULL;

    /* SEEK_END + ftell() asks "how many bytes is this file?"
     * It is fine for normal files; it is not how you read a pipe. */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0 || (unsigned long)size > OMAMD_MAX_FILE_BYTES) {
        fclose(f);
        return NULL;
    }
    rewind(f); /* same as fseek(f, 0, SEEK_SET): go back to byte 0 */

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

/* Read stdin until EOF.  Used by `omamd --html < notes.md`. */
static char *read_entire_stdin(size_t *out_len)
{
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    char tmp[4096];
    size_t n;

    while ((n = fread(tmp, 1, sizeof(tmp), stdin)) > 0) {
        if (n > OMAMD_MAX_FILE_BYTES || len > OMAMD_MAX_FILE_BYTES - n) {
            free(buf);
            return NULL;
        }
        if (len + n + 1 > cap) {
            size_t new_cap = cap ? cap * 2 : 8192;
            char *grown;
            while (new_cap < len + n + 1)
                new_cap *= 2;
            grown = realloc(buf, new_cap);
            if (!grown) {
                free(buf);
                return NULL;
            }
            buf = grown;
            cap = new_cap;
        }
        memcpy(buf + len, tmp, n);
        len += n;
    }
    if (!buf) {
        buf = malloc(1);
        if (!buf)
            return NULL;
    }
    buf[len] = '\0';
    if (out_len)
        *out_len = len;
    return buf;
}

static char *dup_str(const char *s)
{
    size_t n;
    char *out;
    if (!s)
        return NULL;
    n = strlen(s);
    out = malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, s, n + 1);
    return out;
}

static char *dir_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    char *dir;
    size_t n;

    if (!slash)
        return dup_str(".");
    if (slash == path)
        return dup_str("/");
    n = (size_t)(slash - path);
    dir = malloc(n + 1);
    if (!dir)
        return NULL;
    memcpy(dir, path, n);
    dir[n] = '\0';
    return dir;
}

static int ends_with_ci(const char *s, const char *suffix)
{
    size_t ns = strlen(s);
    size_t nt = strlen(suffix);
    size_t i;
    if (nt > ns)
        return 0;
    for (i = 0; i < nt; i++) {
        unsigned char a = (unsigned char)s[ns - nt + i];
        unsigned char b = (unsigned char)suffix[i];
        if (tolower(a) != tolower(b))
            return 0;
    }
    return 1;
}

static int is_markdown_path(const char *path)
{
    return ends_with_ci(path, ".md") ||
           ends_with_ci(path, ".markdown") ||
           ends_with_ci(path, ".mdown") ||
           ends_with_ci(path, ".txt");
}

/* ---------------------------------------------------------------
 * Omarchy theme → CSS
 *
 * Each Omarchy theme ships a colors.toml.  The one that is actually
 * in use is copied to:
 *
 *   ~/.local/state/omarchy/current/theme/colors.toml
 *
 * We read a handful of keys and turn them into CSS variables so the
 * preview follows Super+Ctrl+Shift+Space (or `omarchy theme set`)
 * the next time you open a file.  If that file is missing, we fall
 * back to a dark palette that still looks like a document.
 * --------------------------------------------------------------- */

typedef struct {
    char bg[16];
    char fg[16];
    char muted[16];
    char accent[16];
    char code_bg[16];
    char surface[16];
    char sel[16];
    int dark;
} Palette;

static int valid_hex_color(const char *s)
{
    size_t n;
    size_t i;
    if (!s || s[0] != '#')
        return 0;
    n = strlen(s);
    if (n != 4 && n != 7)
        return 0;
    for (i = 1; i < n; i++) {
        if (!isxdigit((unsigned char)s[i]))
            return 0;
    }
    return 1;
}

static void set_color(char *dst, size_t dst_sz, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < dst_sz && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* Pull `key = "value"` out of a tiny TOML subset.  Real TOML is more
 * than this; theme files only need this shape. */
static int toml_get(const char *text, const char *key, char *out, size_t out_sz)
{
    const char *p = text;
    size_t klen = strlen(key);

    while (*p != '\0') {
        int at_bol = (p == text || p[-1] == '\n');
        if (at_bol && strncmp(p, key, klen) == 0) {
            const char *q = p + klen;
            const char *end;
            size_t n;
            while (*q == ' ' || *q == '\t')
                q++;
            if (*q != '=') {
                p++;
                continue;
            }
            q++;
            while (*q == ' ' || *q == '\t')
                q++;
            if (*q != '"') {
                p++;
                continue;
            }
            q++;
            end = q;
            while (*end != '\0' && *end != '"' && *end != '\n')
                end++;
            n = (size_t)(end - q);
            if (n >= out_sz)
                n = out_sz - 1;
            memcpy(out, q, n);
            out[n] = '\0';
            return 1;
        }
        p++;
    }
    return 0;
}

static void palette_default(Palette *p)
{
    /* A warm-dark fallback if Omarchy's file is not there. */
    snprintf(p->bg, sizeof(p->bg), "%s", "#1e1e2e");
    snprintf(p->fg, sizeof(p->fg), "%s", "#cdd6f4");
    snprintf(p->muted, sizeof(p->muted), "%s", "#6c7086");
    snprintf(p->accent, sizeof(p->accent), "%s", "#89b4fa");
    snprintf(p->code_bg, sizeof(p->code_bg), "%s", "#11111b");
    snprintf(p->surface, sizeof(p->surface), "%s", "#313244");
    snprintf(p->sel, sizeof(p->sel), "%s", "#45475a");
    p->dark = 1;
}

static void palette_load_omarchy(Palette *p)
{
    const char *home = getenv("HOME");
    char path[512];
    char *toml;
    char buf[64];

    palette_default(p);
    if (!home)
        return;
    snprintf(path, sizeof(path),
             "%s/.local/state/omarchy/current/theme/colors.toml", home);
    toml = read_entire_file(path, NULL);
    if (!toml)
        return;

    if (toml_get(toml, "mode", buf, sizeof(buf)))
        p->dark = (strcmp(buf, "light") != 0);

    if (toml_get(toml, "background", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->bg, sizeof(p->bg), buf);
    if (toml_get(toml, "foreground", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->fg, sizeof(p->fg), buf);
    if (toml_get(toml, "muted", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->muted, sizeof(p->muted), buf);
    else if (toml_get(toml, "dark_foreground", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->muted, sizeof(p->muted), buf);
    if (toml_get(toml, "accent", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->accent, sizeof(p->accent), buf);
    else if (toml_get(toml, "blue", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->accent, sizeof(p->accent), buf);
    if (toml_get(toml, "dark_background", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->code_bg, sizeof(p->code_bg), buf);
    else if (toml_get(toml, "darker_background", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->code_bg, sizeof(p->code_bg), buf);
    if (toml_get(toml, "lighter_background", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->surface, sizeof(p->surface), buf);
    if (toml_get(toml, "selection", buf, sizeof(buf)) && valid_hex_color(buf))
        set_color(p->sel, sizeof(p->sel), buf);

    free(toml);
}

static char *build_css(const Palette *p)
{
    char *css;
    /* sizeof a string literal includes the '\0'.  We size generously
     * so snprintf cannot truncate the sheet. */
    const size_t cap = 8192;
    css = malloc(cap);
    if (!css)
        return NULL;

    snprintf(css, cap,
        ":root {\n"
        "  --bg: %s;\n"
        "  --fg: %s;\n"
        "  --muted: %s;\n"
        "  --accent: %s;\n"
        "  --code-bg: %s;\n"
        "  --surface: %s;\n"
        "  --sel: %s;\n"
        "}\n"
        "html, body {\n"
        "  background: var(--bg);\n"
        "  color: var(--fg);\n"
        "  margin: 0;\n"
        "}\n"
        "body {\n"
        "  font-family: system-ui, \"Noto Sans\", \"P052\", sans-serif;\n"
        "  font-size: 17px;\n"
        "  line-height: 1.65;\n"
        "}\n"
        "article.md {\n"
        "  max-width: 42rem;\n"
        "  margin: 0 auto;\n"
        "  padding: 2.4rem 4.2rem 4rem 1.4rem;\n"
        "}\n"
        "h1, h2, h3, h4, h5, h6 {\n"
        "  line-height: 1.25;\n"
        "  font-weight: 650;\n"
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
        "  font-family: \"CaskaydiaMono Nerd Font\", \"JetBrains Mono\",\n"
        "               ui-monospace, monospace;\n"
        "  font-size: 0.88em;\n"
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
        "table { border-collapse: collapse; width: 100%%; }\n"
        "th, td {\n"
        "  border: 1px solid var(--surface);\n"
        "  padding: 0.4em 0.7em;\n"
        "  text-align: left;\n"
        "}\n"
        "th { background: var(--code-bg); }\n"
        "tr:nth-child(even) td { background: color-mix(in srgb, var(--surface) 35%%, transparent); }\n"
        "img { max-width: 100%%; height: auto; border-radius: 6px; }\n"
        "li > p { margin: 0.25em 0; }\n"
        "li:has(> input[type=checkbox]) { list-style: none; margin-left: -1.3em; }\n"
        "input[type=checkbox] { margin-right: 0.45em; }\n"
        "::selection { background: var(--sel); }\n",
        p->bg, p->fg, p->muted, p->accent, p->code_bg, p->surface, p->sel);
    return css;
}

/* GTK's own stylesheet for the chrome we draw: a chrome-less window
 * and the floating mode toggle.  This is *not* the document
 * CSS — that goes into WebKit.  GtkCssProvider is how you add CSS
 * to GTK widgets at runtime. */
static void apply_ui_css(App *app, const Palette *p)
{
    char css[2560];

    snprintf(css, sizeof(css),
        "window {"
        "  background-color: %s;"
        "}"
        "webview {"
        "  background-color: %s;"
        "}"
        "textview, textview text {"
        "  background-color: %s;"
        "  color: %s;"
        "}"
        "#omamd-mode-toggle {"
        "  min-width: 44px;"
        "  min-height: 44px;"
        "  padding: 0;"
        "  border-radius: 22px;"
        "  background-image: none;"
        "  background-color: alpha(%s, 0.38);"
        "  border: 1px solid alpha(%s, 0.42);"
        "  box-shadow: none;"
        "  outline: none;"
        "  color: %s;"
        "}"
        "#omamd-mode-toggle:hover {"
        "  background-color: alpha(%s, 0.66);"
        "  border-color: alpha(%s, 0.7);"
        "}"
        "#omamd-mode-toggle:active {"
        "  background-color: alpha(%s, 0.82);"
        "}",
        p->bg, p->bg,
        p->bg, p->fg,
        p->code_bg, p->fg, p->fg,
        p->code_bg, p->accent,
        p->code_bg);

    /* One provider for the life of the app.  Reloading its data
     * updates every widget; stacking a new provider each theme
     * change would leak old sheets. */
    if (!app->ui_css) {
        app->ui_css = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(app->ui_css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    gtk_css_provider_load_from_data(app->ui_css, css, -1, NULL);
}

static void escape_html_str(GString *out, const char *s)
{
    for (; s && *s; s++) {
        switch (*s) {
        case '&':  g_string_append(out, "&amp;");  break;
        case '<':  g_string_append(out, "&lt;");   break;
        case '>':  g_string_append(out, "&gt;");   break;
        case '"':  g_string_append(out, "&quot;"); break;
        default:   g_string_append_c(out, *s);     break;
        }
    }
}

/* Wrap a body fragment in a full HTML document.  GString is GLib's
 * growable string — the same idea as Buf in markdown.c, already
 * written for us because we linked GTK. */
static char *wrap_document(const char *title, const char *css, const char *body)
{
    GString *s = g_string_new(NULL);
    g_string_append(s,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>");
    escape_html_str(s, title ? title : "omamd");
    g_string_append(s, "</title><style>");
    g_string_append(s, css ? css : "");
    g_string_append(s, "</style></head><body><article class=\"md\">");
    g_string_append(s, body ? body : "");
    g_string_append(s, "</article><div id=\"omamd-end\"></div></body></html>");
    return g_string_free(s, FALSE); /* FALSE = hand the bytes to the caller */
}

/* The page shown when nothing is open yet. */
static const char *const WELCOME_MD =
    "# omamd\n"
    "\n"
    "A small Markdown viewer.  Open a file with **Ctrl+O**, drop one "
    "onto this window, or start it as:\n"
    "\n"
    "```\n"
    "omamd notes.md\n"
    "```\n"
    "\n"
    "The round button in the top-right corner switches between the "
    "rendered page and the Markdown source (`Ctrl+1` / `Ctrl+2`).\n"
    "\n"
    "## What the parser understands\n"
    "\n"
    "- Headings, **bold**, *italic*, ~~strike~~, `inline code`\n"
    "- Lists, including task boxes\n"
    "- Fenced code, quotes, tables, links, images\n"
    "\n"
    "> Edit the C, run `make`, and this page is yours to change.\n"
    "\n"
    "Read `src/markdown.h` then `src/markdown.c` then this file, `src/main.c`.\n";

/* ---------------------------------------------------------------
 * Loading a document into the widgets
 * --------------------------------------------------------------- */

static void source_follow_stop(App *app)
{
    if (app->source_tick && app->source_scroll) {
        gtk_widget_remove_tick_callback(app->source_scroll, app->source_tick);
        app->source_tick = 0;
    }
}

static gboolean on_source_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer user_data)
{
    App *app = user_data;
    gint64 now, dur;
    gdouble t, e, target;
    GtkAdjustment *adj;

    (void)widget;
    adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app->source_scroll));
    target = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
    if (target < 0)
        target = 0;
    /* The buffer can still be growing its height as GTK lays out. */
    app->scroll_to = target;

    now = gdk_frame_clock_get_frame_time(clock);
    if (app->scroll_t0 == 0)
        app->scroll_t0 = now;
    dur = 450 * 1000; /* microseconds */
    t = (gdouble)(now - app->scroll_t0) / (gdouble)dur;
    if (t >= 1.0) {
        gtk_adjustment_set_value(adj, app->scroll_to);
        app->source_tick = 0;
        return G_SOURCE_REMOVE;
    }
    e = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    gtk_adjustment_set_value(adj, app->scroll_from + (app->scroll_to - app->scroll_from) * e);
    return G_SOURCE_CONTINUE;
}

static gboolean follow_source_idle(gpointer user_data)
{
    App *app = user_data;
    GtkAdjustment *adj;
    gdouble target;

    app->follow_source_idle_id = 0;
    if (!app->follow_source || !app->source_scroll)
        return G_SOURCE_REMOVE;
    app->follow_source = 0;

    adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app->source_scroll));
    target = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
    if (target < 0)
        target = 0;

    source_follow_stop(app);
    app->scroll_from = gtk_adjustment_get_value(adj);
    app->scroll_to = target;
    app->scroll_t0 = 0; /* first tick stamps the clock so the ease is in sync */
    app->source_tick = gtk_widget_add_tick_callback(
        app->source_scroll, on_source_tick, app, NULL);
    return G_SOURCE_REMOVE;
}

static void follow_preview_now(App *app)
{
    static const char *script =
        "(function(){"
        "var r=document.scrollingElement||document.documentElement;"
        "var t=Math.max(0,r.scrollHeight-r.clientHeight);"
        "var from=Math.max(0,t-r.clientHeight*2.5);"
        "r.scrollTop=from;"
        "requestAnimationFrame(function(){"
        "try{r.scrollTo({top:t,behavior:'smooth'});}"
        "catch(e){r.scrollTop=t;}"
        "});"
        "})();";

    webkit_web_view_evaluate_javascript(
        WEBKIT_WEB_VIEW(app->web_view), script, -1,
        NULL, NULL, NULL, NULL, NULL);
}

static void on_load_changed(WebKitWebView *view, WebKitLoadEvent event, gpointer user_data)
{
    App *app = user_data;
    (void)view;
    if (event != WEBKIT_LOAD_FINISHED)
        return;
    if (!app->follow_preview)
        return;
    app->follow_preview = 0;
    follow_preview_now(app);
}

static void show_error(App *app, const char *msg)
{
    GtkWidget *d = gtk_message_dialog_new(
        app->window ? GTK_WINDOW(app->window) : NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE,
        "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static void app_render_text(App *app, const char *md, size_t n, const char *title)
{
    char *fragment;
    char *page;
    GtkTextBuffer *buf;

    fragment = markdown_to_html(md, n);
    if (!fragment) {
        show_error(app, "Out of memory while converting Markdown.");
        return;
    }
    page = wrap_document(title, app->css, fragment);
    free(fragment);
    if (!page) {
        show_error(app, "Out of memory while wrapping HTML.");
        return;
    }

    webkit_web_view_load_html(WEBKIT_WEB_VIEW(app->web_view), page,
                              app->base_uri ? app->base_uri : "about:blank");
    g_free(page);

    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->text_view));
    gtk_text_buffer_set_text(buf, md ? md : "", (gint)n);

    {
        char win_title[512];
        snprintf(win_title, sizeof(win_title), "%s — omamd", title ? title : "omamd");
        gtk_window_set_title(GTK_WINDOW(app->window), win_title);
    }

    if (app->follow_source && !app->follow_source_idle_id)
        app->follow_source_idle_id = g_timeout_add(50, follow_source_idle, app);
}

static void app_show_welcome(App *app)
{
    free(app->path);
    app->path = NULL;
    free(app->doc_dir);
    app->doc_dir = NULL;
    g_free(app->base_uri);
    app->base_uri = NULL;
    free(app->source);
    app->source = dup_str(WELCOME_MD);
    app_render_text(app, WELCOME_MD, strlen(WELCOME_MD), "welcome");
}

static void app_clear_monitor(App *app)
{
    if (app->reload_timeout) {
        g_source_remove(app->reload_timeout);
        app->reload_timeout = 0;
    }
    if (app->monitor) {
        g_object_unref(app->monitor);
        app->monitor = NULL;
    }
}

static gboolean app_reload_now(gpointer user_data);

static void on_file_changed(GFileMonitor *monitor, GFile *file, GFile *other,
                            GFileMonitorEvent event, gpointer user_data)
{
    App *app = user_data;
    (void)monitor;
    (void)file;
    (void)other;

    /* Editors often write a temp file and rename it.  Several events
     * can fire for one save, so we wait a moment and reload once.
     * The reload follows the new end of the file (AI append). */
    if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
        event != G_FILE_MONITOR_EVENT_CHANGED &&
        event != G_FILE_MONITOR_EVENT_CREATED &&
        event != G_FILE_MONITOR_EVENT_MOVED_IN &&
        event != G_FILE_MONITOR_EVENT_RENAMED)
        return;

    if (app->reload_timeout)
        g_source_remove(app->reload_timeout);
    app->reload_timeout = g_timeout_add(120, app_reload_now, app);
}

static void app_watch(App *app, const char *path)
{
    GFile *gf;
    GError *err = NULL;

    app_clear_monitor(app);
    gf = g_file_new_for_path(path);
    app->monitor = g_file_monitor_file(gf, G_FILE_MONITOR_WATCH_MOVES, NULL, &err);
    g_object_unref(gf);
    if (!app->monitor) {
        g_clear_error(&err);
        return;
    }
    g_signal_connect(app->monitor, "changed", G_CALLBACK(on_file_changed), app);
}

static int app_load_path(App *app, const char *path, int follow)
{
    size_t n = 0;
    char *md = read_entire_file(path, &n);
    char *dir;
    const char *slash;
    const char *title;

    if (!md) {
        char msg[1024];
        if (follow)
            return 0; /* file is often empty for a moment during an atomic save */
        snprintf(msg, sizeof(msg), "Could not read:\n%s", path);
        show_error(app, msg);
        return 0;
    }

    app->follow_preview = follow;
    app->follow_source = follow;

    /* Reload passes app->path as `path`.  Freeing it first would
     * leave `path` dangling (and the window title as garbage). */
    if (path != app->path) {
        free(app->path);
        app->path = dup_str(path);
        path = app->path;
    }

    g_free(app->base_uri);
    free(app->doc_dir);
    dir = dir_of(path);
    app->doc_dir = dir ? realpath(dir, NULL) : NULL;
    app->base_uri = g_filename_to_uri(dir, NULL, NULL);
    /* A directory URI must end in / so "pic.png" resolves next to the file. */
    if (app->base_uri && app->base_uri[strlen(app->base_uri) - 1] != '/') {
        char *with_slash = g_strconcat(app->base_uri, "/", NULL);
        g_free(app->base_uri);
        app->base_uri = with_slash;
    }
    free(dir);

    free(app->source);
    app->source = md;

    slash = strrchr(path, '/');
    title = slash ? slash + 1 : path;
    app_render_text(app, md, n, title);
    app_watch(app, path);
    return 1;
}

static gboolean app_reload_now(gpointer user_data)
{
    App *app = user_data;
    app->reload_timeout = 0;
    if (app->path)
        app_load_path(app, app->path, 1);
    return G_SOURCE_REMOVE;
}

/* ---------------------------------------------------------------
 * GTK callbacks
 *
 * A callback is an ordinary function.  We pass its address to
 * g_signal_connect.  GTK calls it later, when the event happens.
 *
 * G_CALLBACK() is a cast.  GTK is written in C and stores callbacks
 * as a generic function pointer; the macro quiets the type warning.
 * --------------------------------------------------------------- */

static void on_open_clicked(GtkButton *button, gpointer user_data)
{
    App *app = user_data;
    GtkWidget *dialog;
    GtkFileFilter *md, *any;

    (void)button;
    dialog = gtk_file_chooser_dialog_new(
        "Open Markdown",
        GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    md = gtk_file_filter_new();
    gtk_file_filter_set_name(md, "Markdown");
    gtk_file_filter_add_pattern(md, "*.md");
    gtk_file_filter_add_pattern(md, "*.markdown");
    gtk_file_filter_add_pattern(md, "*.mdown");
    gtk_file_filter_add_pattern(md, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), md);

    any = gtk_file_filter_new();
    gtk_file_filter_set_name(any, "All files");
    gtk_file_filter_add_pattern(any, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), any);

    if (app->path)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), app->path);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            app_load_path(app, filename, 0);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_reload_clicked(GtkButton *button, gpointer user_data)
{
    App *app = user_data;
    (void)button;
    if (app->path)
        app_load_path(app, app->path, 0);
}

static void app_clear_theme_watch(App *app);

static void on_destroy(GtkWidget *widget, gpointer user_data)
{
    App *app = user_data;
    (void)widget;
    app_clear_monitor(app);
    if (app->follow_source_idle_id) {
        g_source_remove(app->follow_source_idle_id);
        app->follow_source_idle_id = 0;
    }
    source_follow_stop(app);
    app_clear_theme_watch(app);
    if (app->ui_css) {
        gtk_style_context_remove_provider_for_screen(
            gdk_screen_get_default(), GTK_STYLE_PROVIDER(app->ui_css));
        g_object_unref(app->ui_css);
        app->ui_css = NULL;
    }
    free(app->path);
    free(app->doc_dir);
    g_free(app->base_uri);
    free(app->source);
    free(app->css);
    free(app);
    gtk_main_quit();
}

/* True if `path` is inside `dir` after resolving `.` and `..`. */
static int path_is_under_dir(const char *path, const char *dir)
{
    char *real_path;
    char *real_dir;
    size_t n;
    int ok;

    if (!path || !dir)
        return 0;
    real_path = realpath(path, NULL);
    real_dir = realpath(dir, NULL);
    if (!real_path || !real_dir) {
        free(real_path);
        free(real_dir);
        return 0;
    }
    n = strlen(real_dir);
    ok = strncmp(real_path, real_dir, n) == 0 &&
         (real_path[n] == '\0' || real_path[n] == '/');
    free(real_path);
    free(real_dir);
    return ok;
}

static int file_uri_is_under_doc(App *app, const char *uri)
{
    GError *err = NULL;
    char *path;
    int ok;

    if (!app->doc_dir)
        return 0;
    path = g_filename_from_uri(uri, NULL, &err);
    g_clear_error(&err);
    if (!path)
        return 0;
    ok = path_is_under_dir(path, app->doc_dir);
    g_free(path);
    return ok;
}

static int uri_is_allowed_resource(App *app, const char *uri)
{
    if (!uri)
        return 0;
    if (g_str_has_prefix(uri, "about:") || g_str_has_prefix(uri, "data:image/"))
        return 1;
    if (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://"))
        return 1;
    if (g_str_has_prefix(uri, "file://"))
        return file_uri_is_under_doc(app, uri);
    return 0;
}

static void open_in_browser(App *app, const char *uri)
{
    gtk_show_uri_on_window(GTK_WINDOW(app->window), uri, GDK_CURRENT_TIME, NULL);
}

/* Clicked links: http(s) opens in the real browser.  A local .md/.txt
 * file is opened in omamd only if it sits under the current document's
 * directory.  Everything else — javascript:, file:///etc/passwd,
 * target=_blank to a random path — is dropped.  Image subresources
 * are checked the same way via RESPONSE decisions. */
static gboolean on_decide_policy(WebKitWebView *web_view,
                                 WebKitPolicyDecision *decision,
                                 WebKitPolicyDecisionType type,
                                 gpointer user_data)
{
    App *app = user_data;
    WebKitURIRequest *req = NULL;
    const char *uri = NULL;

    (void)web_view;

    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision *rd = WEBKIT_RESPONSE_POLICY_DECISION(decision);
        if (webkit_response_policy_decision_is_main_frame_main_resource(rd))
            return FALSE;
        req = webkit_response_policy_decision_get_request(rd);
        uri = req ? webkit_uri_request_get_uri(req) : NULL;
        if (!uri_is_allowed_resource(app, uri)) {
            webkit_policy_decision_ignore(decision);
            return TRUE;
        }
        return FALSE;
    }

    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION ||
        type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        WebKitNavigationPolicyDecision *nd = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        WebKitNavigationAction *action =
            webkit_navigation_policy_decision_get_navigation_action(nd);
        WebKitNavigationType nav = webkit_navigation_action_get_navigation_type(action);

        req = webkit_navigation_action_get_request(action);
        uri = req ? webkit_uri_request_get_uri(req) : NULL;

        if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
            nav != WEBKIT_NAVIGATION_TYPE_LINK_CLICKED)
            return FALSE; /* load_html of our own page */

        if (uri && (g_str_has_prefix(uri, "http://") ||
                    g_str_has_prefix(uri, "https://") ||
                    g_str_has_prefix(uri, "mailto:"))) {
            open_in_browser(app, uri);
            webkit_policy_decision_ignore(decision);
            return TRUE;
        }

        if (uri && g_str_has_prefix(uri, "file://")) {
            GError *err = NULL;
            char *path = g_filename_from_uri(uri, NULL, &err);
            g_clear_error(&err);
            if (path && is_markdown_path(path) &&
                app->doc_dir && path_is_under_dir(path, app->doc_dir)) {
                app_load_path(app, path, 0);
                g_free(path);
                webkit_policy_decision_ignore(decision);
                return TRUE;
            }
            g_free(path);
        }

        webkit_policy_decision_ignore(decision);
        return TRUE;
    }

    return FALSE;
}

static void on_drag_data(GtkWidget *widget, GdkDragContext *ctx, gint x, gint y,
                         GtkSelectionData *data, guint info, guint time,
                         gpointer user_data)
{
    App *app = user_data;
    gchar **uris;
    (void)widget;
    (void)x;
    (void)y;
    (void)info;

    uris = gtk_selection_data_get_uris(data);
    if (uris && uris[0]) {
        GError *err = NULL;
        char *path = g_filename_from_uri(uris[0], NULL, &err);
        if (path)
            app_load_path(app, path, 0);
        g_free(path);
        g_clear_error(&err);
        gtk_drag_finish(ctx, TRUE, FALSE, time);
    } else {
        gtk_drag_finish(ctx, FALSE, FALSE, time);
    }
    g_strfreev(uris);
}

static void set_mode(App *app, const char *name);
static GtkWidget *mode_icon_image(const char *color, int source);

static gboolean on_key(GtkWidget *widget, GdkEventKey *e, gpointer user_data)
{
    App *app = user_data;
    GdkModifierType mods = e->state & gtk_accelerator_get_default_mod_mask();
    (void)widget;

    if (mods == GDK_CONTROL_MASK &&
        (e->keyval == GDK_KEY_o || e->keyval == GDK_KEY_O)) {
        on_open_clicked(NULL, app);
        return TRUE;
    }
    if ((mods == GDK_CONTROL_MASK &&
         (e->keyval == GDK_KEY_r || e->keyval == GDK_KEY_R)) ||
        e->keyval == GDK_KEY_F5) {
        on_reload_clicked(NULL, app);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK &&
        (e->keyval == GDK_KEY_q || e->keyval == GDK_KEY_Q)) {
        gtk_widget_destroy(app->window);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && e->keyval == GDK_KEY_1) {
        set_mode(app, "preview");
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && e->keyval == GDK_KEY_2) {
        set_mode(app, "source");
        return TRUE;
    }
    return FALSE;
}

/*
 * Tiny SVG → GtkImage.  `</>` when you are looking at the rendered
 * page (click to see source); an eye when you are in source (click
 * to preview).  Drawn as SVG so we can colour it with the Omarchy
 * palette instead of hoping a font glyph exists.
 */
static GtkWidget *mode_icon_image(const char *color, int source)
{
    char svg[900];
    gsize svg_len;
    GInputStream *in;
    GdkPixbuf *pb;
    GtkWidget *img;
    int scale = 1;
    GdkDisplay *dpy = gdk_display_get_default();

    if (dpy) {
        GdkMonitor *mon = gdk_display_get_primary_monitor(dpy);
        if (mon)
            scale = gdk_monitor_get_scale_factor(mon);
        if (scale < 1)
            scale = 1;
    }

    if (source) {
        /* Eye: you are in source, click to preview. */
        snprintf(svg, sizeof(svg),
            "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24'"
            " viewBox='0 0 24 24' fill='none'>"
            "<path d='M2.4 12s3.6-6.2 9.6-6.2S21.6 12 21.6 12"
            "s-3.6 6.2-9.6 6.2S2.4 12 2.4 12z'"
            " stroke='%s' stroke-width='1.9' stroke-linecap='round'"
            " stroke-linejoin='round'/>"
            "<circle cx='12' cy='12' r='2.5' fill='%s'/>"
            "</svg>",
            color, color);
    } else {
        /* Chevrons: you are in preview, click to see the markup. */
        snprintf(svg, sizeof(svg),
            "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24'"
            " viewBox='0 0 24 24' fill='none'>"
            "<path d='M8.4 5.4L3 12l5.4 6.6' stroke='%s' stroke-width='2.05'"
            " stroke-linecap='round' stroke-linejoin='round'/>"
            "<path d='M15.6 5.4L21 12l-5.4 6.6' stroke='%s' stroke-width='2.05'"
            " stroke-linecap='round' stroke-linejoin='round'/>"
            "<path d='M13.55 5L10.45 19' stroke='%s' stroke-width='2.05'"
            " stroke-linecap='round'/>"
            "</svg>",
            color, color, color);
    }

    svg_len = strlen(svg);
    in = g_memory_input_stream_new_from_data(g_strdup(svg), (gssize)svg_len, g_free);
    pb = gdk_pixbuf_new_from_stream_at_scale(in, 22 * scale, 22 * scale, TRUE, NULL, NULL);
    g_object_unref(in);
    if (!pb)
        return gtk_image_new_from_icon_name(
            source ? "view-reveal-symbolic" : "text-x-generic-symbolic",
            GTK_ICON_SIZE_BUTTON);

    if (scale > 1) {
        cairo_surface_t *surf = gdk_cairo_surface_create_from_pixbuf(pb, scale, NULL);
        img = gtk_image_new_from_surface(surf);
        cairo_surface_destroy(surf);
    } else {
        img = gtk_image_new_from_pixbuf(pb);
    }
    g_object_unref(pb);
    return img;
}

static void set_mode(App *app, const char *name)
{
    int source = (strcmp(name, "source") == 0);

    gtk_stack_set_visible_child_name(GTK_STACK(app->stack),
                                     source ? "source" : "preview");
    gtk_button_set_image(GTK_BUTTON(app->mode_btn),
                         mode_icon_image(app->icon_fg, source));
    gtk_widget_set_tooltip_text(
        app->mode_btn,
        source ? "Preview (Ctrl+1)" : "Source (Ctrl+2)");
}

static void on_mode_clicked(GtkButton *button, gpointer user_data)
{
    App *app = user_data;
    const char *cur;
    (void)button;
    cur = gtk_stack_get_visible_child_name(GTK_STACK(app->stack));
    if (cur && strcmp(cur, "source") == 0)
        set_mode(app, "preview");
    else
        set_mode(app, "source");
}

/* ---------------------------------------------------------------
 * Follow Omarchy theme changes
 *
 * `omarchy theme set` does this (in order):
 *   1. rm -rf ~/.local/state/omarchy/current/theme
 *   2. mv a freshly copied theme directory into that path
 *   3. rewrite current/theme.name
 *
 * A file monitor on colors.toml dies at step 1, so we also watch
 * the stable `current/` directory.  Events are debounced so we
 * apply once, after the new colors.toml is in place.
 * --------------------------------------------------------------- */

static void omarchy_current_path(char *out, size_t out_sz, const char *leaf)
{
    const char *home = getenv("HOME");
    if (!home) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_sz, "%s/.local/state/omarchy/current/%s", home, leaf);
}

static void app_watch_colors_file(App *app);

static gboolean app_apply_theme_now(gpointer user_data)
{
    App *app = user_data;
    Palette pal;
    GdkRGBA bg;
    char *css;
    char colors_path[512];
    const char *page;
    const char *title;

    app->theme_timeout = 0;

    omarchy_current_path(colors_path, sizeof(colors_path), "theme/colors.toml");
    if (colors_path[0] == '\0' ||
        !g_file_test(colors_path, G_FILE_TEST_IS_REGULAR)) {
        /* Between rm and mv.  Try again shortly. */
        app->theme_timeout = g_timeout_add(80, app_apply_theme_now, app);
        return G_SOURCE_REMOVE;
    }

    palette_load_omarchy(&pal);
    css = build_css(&pal);
    if (!css)
        return G_SOURCE_REMOVE;

    if (app->css && strcmp(app->css, css) == 0) {
        free(css);
        app_watch_colors_file(app);
        return G_SOURCE_REMOVE;
    }

    free(app->css);
    app->css = css;
    set_color(app->icon_fg, sizeof(app->icon_fg), pal.fg);
    apply_ui_css(app, &pal);
    if (gdk_rgba_parse(&bg, pal.bg))
        webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(app->web_view), &bg);

    page = gtk_stack_get_visible_child_name(GTK_STACK(app->stack));
    if (app->source) {
        if (app->path) {
            const char *slash = strrchr(app->path, '/');
            title = slash ? slash + 1 : app->path;
        } else {
            title = "welcome";
        }
        app_render_text(app, app->source, strlen(app->source), title);
    }
    set_mode(app, (page && strcmp(page, "source") == 0) ? "source" : "preview");
    app_watch_colors_file(app);
    return G_SOURCE_REMOVE;
}

static void on_theme_fs_event(GFileMonitor *monitor, GFile *file, GFile *other,
                              GFileMonitorEvent event, gpointer user_data)
{
    App *app = user_data;
    (void)monitor;
    (void)file;
    (void)other;
    (void)event;

    if (app->theme_timeout)
        g_source_remove(app->theme_timeout);
    app->theme_timeout = g_timeout_add(250, app_apply_theme_now, app);
}

static void app_watch_colors_file(App *app)
{
    char path[512];
    GFile *gf;

    if (app->theme_colors_mon) {
        g_object_unref(app->theme_colors_mon);
        app->theme_colors_mon = NULL;
    }
    omarchy_current_path(path, sizeof(path), "theme/colors.toml");
    if (path[0] == '\0' || !g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return;
    gf = g_file_new_for_path(path);
    app->theme_colors_mon = g_file_monitor_file(
        gf, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    g_object_unref(gf);
    if (app->theme_colors_mon)
        g_signal_connect(app->theme_colors_mon, "changed",
                         G_CALLBACK(on_theme_fs_event), app);
}

static void app_watch_theme(App *app)
{
    char path[512];
    GFile *gf;

    omarchy_current_path(path, sizeof(path), "");
    if (path[0] == '\0')
        return;
    /* Trailing slash from "%s/" + "" — strip it for g_file_new. */
    {
        size_t n = strlen(path);
        if (n > 0 && path[n - 1] == '/')
            path[n - 1] = '\0';
    }
    gf = g_file_new_for_path(path);
    app->theme_dir_mon = g_file_monitor_directory(
        gf, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    g_object_unref(gf);
    if (app->theme_dir_mon)
        g_signal_connect(app->theme_dir_mon, "changed",
                         G_CALLBACK(on_theme_fs_event), app);

    app_watch_colors_file(app);
}

static void app_clear_theme_watch(App *app)
{
    if (app->theme_timeout) {
        g_source_remove(app->theme_timeout);
        app->theme_timeout = 0;
    }
    if (app->theme_dir_mon) {
        g_object_unref(app->theme_dir_mon);
        app->theme_dir_mon = NULL;
    }
    if (app->theme_colors_mon) {
        g_object_unref(app->theme_colors_mon);
        app->theme_colors_mon = NULL;
    }
}

/* ---------------------------------------------------------------
 * Build the window
 * --------------------------------------------------------------- */

static void build_ui(App *app)
{
    GtkWidget *overlay, *scroll;
    WebKitSettings *wk;

    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "omamd");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 860, 960);
    gtk_window_set_icon_name(GTK_WINDOW(app->window), "text-x-generic");
    /* No titlebar, no GTK close/min/max.  Hyprland still draws the
     * window border.  Close with Ctrl+Q (or the compositor's kill). */
    gtk_window_set_decorated(GTK_WINDOW(app->window), FALSE);

    app->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app->stack),
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    /* WebKit draws the rendered Markdown.  JavaScript is off: a
     * viewer should not run scripts that happen to be in a .md file. */
    app->web_view = webkit_web_view_new();
    wk = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(app->web_view));
    /* The JS engine is on so we can ease-scroll after a live reload.
     * Markup is off: <script> tags, event handlers, and javascript:
     * URLs in the page do not run.  The only script we execute is the
     * short scroll helper we pass to evaluate_javascript(). */
    webkit_settings_set_enable_javascript(wk, TRUE);
    webkit_settings_set_enable_javascript_markup(wk, FALSE);
    webkit_settings_set_allow_file_access_from_file_urls(wk, FALSE);
    webkit_settings_set_allow_universal_access_from_file_urls(wk, FALSE);
    webkit_settings_set_enable_html5_local_storage(wk, FALSE);
    webkit_settings_set_enable_html5_database(wk, FALSE);
    webkit_settings_set_enable_media_stream(wk, FALSE);
    webkit_settings_set_enable_webgl(wk, FALSE);
    /* Offscreen compositing can draw WebKit *over* GTK overlay
     * widgets.  Turning this off keeps the floating buttons on top. */
    webkit_settings_set_hardware_acceleration_policy(
        wk, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

    app->text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->text_view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 16);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 64);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 16);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 16);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), app->text_view);
    app->source_scroll = scroll;

    gtk_stack_add_named(GTK_STACK(app->stack), app->web_view, "preview");
    gtk_stack_add_named(GTK_STACK(app->stack), scroll, "source");

    /* GtkOverlay stacks widgets.  The main child (the document)
     * fills the window and scrolls.  Overlay children keep their
     * own size and stay pinned to a corner, so they do not move
     * when the Markdown is long. */
    overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(overlay), app->stack);

    app->mode_btn = gtk_button_new();
    gtk_widget_set_name(app->mode_btn, "omamd-mode-toggle");
    gtk_button_set_relief(GTK_BUTTON(app->mode_btn), GTK_RELIEF_NONE);
    gtk_button_set_always_show_image(GTK_BUTTON(app->mode_btn), TRUE);
    gtk_widget_set_halign(app->mode_btn, GTK_ALIGN_END);
    gtk_widget_set_valign(app->mode_btn, GTK_ALIGN_START);
    gtk_widget_set_margin_top(app->mode_btn, 14);
    gtk_widget_set_margin_end(app->mode_btn, 14);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app->mode_btn);

    gtk_container_add(GTK_CONTAINER(app->window), overlay);

    /* Drop a .md file on the window to open it. */
    gtk_drag_dest_set(app->window, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(app->window);

    g_signal_connect(app->mode_btn, "clicked", G_CALLBACK(on_mode_clicked), app);
    g_signal_connect(app->window, "destroy", G_CALLBACK(on_destroy), app);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(on_key), app);
    g_signal_connect(app->window, "drag-data-received", G_CALLBACK(on_drag_data), app);
    g_signal_connect(app->web_view, "decide-policy", G_CALLBACK(on_decide_policy), app);
    g_signal_connect(app->web_view, "load-changed", G_CALLBACK(on_load_changed), app);

    app_apply_theme_now(app);
    app_watch_theme(app);
}

static void usage(FILE *out)
{
    fprintf(out,
            "omamd %s — a small Markdown viewer\n"
            "\n"
            "Usage:\n"
            "  omamd [file.md]         open in a window\n"
            "  omamd --html [file.md]  print HTML (stdin if no file)\n"
            "  omamd --help            this text\n"
            "\n"
            "Keys:  Ctrl+O open   Ctrl+R reload   Ctrl+1 preview\n"
            "       Ctrl+2 source Ctrl+Q quit     F5 reload\n",
            OMAMD_VERSION);
}

static int run_html_mode(const char *path)
{
    size_t n = 0;
    char *md;
    char *fragment;
    char *page;
    Palette pal;
    char *css;
    const char *title = "omamd";

    if (path) {
        const char *slash;
        md = read_entire_file(path, &n);
        if (!md) {
            fprintf(stderr, "omamd: cannot read %s\n", path);
            return 1;
        }
        slash = strrchr(path, '/');
        title = slash ? slash + 1 : path;
    } else {
        md = read_entire_stdin(&n);
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
    palette_load_omarchy(&pal);
    css = build_css(&pal);
    page = wrap_document(title, css, fragment);
    free(fragment);
    free(css);
    if (!page) {
        fprintf(stderr, "omamd: out of memory\n");
        return 1;
    }
    fputs(page, stdout);
    g_free(page);
    return 0;
}

int main(int argc, char **argv)
{
    int html_mode = 0;
    const char *path = NULL;
    int i;
    App *app;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("omamd %s\n", OMAMD_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--html") == 0) {
            html_mode = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "omamd: unknown option %s\n", argv[i]);
            usage(stderr);
            return 2;
        }
        path = argv[i];
    }

    if (html_mode)
        return run_html_mode(path);

    /* gtk_init may strip GTK-specific arguments from argv. */
    gtk_init(&argc, &argv);

    app = calloc(1, sizeof(App));
    if (!app)
        return 1;
    build_ui(app);

    if (path) {
        if (!app_load_path(app, path, 0))
            app_show_welcome(app);
    } else {
        app_show_welcome(app);
    }

    gtk_widget_show_all(app->window);
    gtk_main();
    return 0;
}
