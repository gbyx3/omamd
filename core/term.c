/*
 * term.c — Markdown on a terminal (SSH, no display).
 *
 * We reuse markdown_to_html() so the parser stays in one place, then
 * walk the HTML we ourselves produced and turn tags into ANSI.
 * A TTY gets a tiny pager; a pipe just gets the text.
 */

#include "term.h"
#include "markdown.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

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
    buf_put(b, s, strlen(s));
}

static void buf_putc(Buf *b, char c)
{
    if (!buf_reserve(b, 1))
        return;
    b->data[b->len++] = c;
}

static char *buf_take(Buf *b)
{
    char *s;
    if (b->oom) {
        free(b->data);
        return NULL;
    }
    if (!buf_reserve(b, 1)) {
        free(b->data);
        return NULL;
    }
    b->data[b->len] = '\0';
    s = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return s;
}

static unsigned ch_r(unsigned rgb) { return (rgb >> 16) & 255; }
static unsigned ch_g(unsigned rgb) { return (rgb >> 8) & 255; }
static unsigned ch_b(unsigned rgb) { return rgb & 255; }

typedef struct {
    Buf out;
    const TermPalette *pal;
    int bold;
    int em;
    int del;
    int code;
    int pre;
    int quote;
    int heading; /* 1-6, or 0 */
    int in_a;
    int at_bol;
    int list_depth;
    int list_ol[32];
    int list_n[32];
    int in_li;
    int skip_ws;
    char href[512];
    size_t href_len;
    char atext[512];
    size_t atext_len;
} Conv;

static void conv_sgr(Conv *c)
{
    char tmp[96];
    const TermPalette *p = c->pal;

    if (!p || !p->color)
        return;
    buf_puts(&c->out, "\033[0m");
    if (c->pre || c->code) {
        snprintf(tmp, sizeof(tmp), "\033[38;2;%u;%u;%um",
                 ch_r(p->muted), ch_g(p->muted), ch_b(p->muted));
        buf_puts(&c->out, tmp);
        if (c->code && !c->pre) {
            snprintf(tmp, sizeof(tmp), "\033[48;2;%u;%u;%um",
                     ch_r(p->code_bg), ch_g(p->code_bg), ch_b(p->code_bg));
            buf_puts(&c->out, tmp);
        }
    } else if (c->heading) {
        snprintf(tmp, sizeof(tmp), "\033[1m\033[38;2;%u;%u;%um",
                 ch_r(p->accent), ch_g(p->accent), ch_b(p->accent));
        buf_puts(&c->out, tmp);
    } else {
        snprintf(tmp, sizeof(tmp), "\033[38;2;%u;%u;%um",
                 ch_r(p->fg), ch_g(p->fg), ch_b(p->fg));
        buf_puts(&c->out, tmp);
    }
    if (c->bold && !c->heading)
        buf_puts(&c->out, "\033[1m");
    if (c->em)
        buf_puts(&c->out, "\033[3m");
    if (c->del)
        buf_puts(&c->out, "\033[9m");
    if (c->quote && !c->heading)
        buf_puts(&c->out, "\033[2m");
    if (c->in_a)
        buf_puts(&c->out, "\033[4m");
}

static void conv_bol_prefix(Conv *c)
{
    int i;
    if (!c->at_bol)
        return;
    c->at_bol = 0;
    for (i = 0; i < c->quote; i++)
        buf_puts(&c->out, "│ ");
    conv_sgr(c);
}

static void conv_nl(Conv *c)
{
    if (c->pal && c->pal->color)
        buf_puts(&c->out, "\033[0m");
    buf_putc(&c->out, '\n');
    c->at_bol = 1;
}

static void conv_text(Conv *c, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (c->in_a && c->atext_len + 1 < sizeof(c->atext))
            c->atext[c->atext_len++] = (char)ch;
        if (!c->pre && (ch == '\n' || ch == '\r')) {
            if (c->skip_ws)
                continue;
            conv_bol_prefix(c);
            buf_putc(&c->out, ' ');
            c->skip_ws = 1;
            continue;
        }
        if (!c->pre && c->skip_ws && (ch == ' ' || ch == '\t'))
            continue;
        c->skip_ws = 0;
        conv_bol_prefix(c);
        buf_putc(&c->out, (char)ch);
    }
}

static size_t decode_entity(const char *s, size_t n, char *outc)
{
    if (n >= 5 && strncmp(s, "&amp;", 5) == 0) {
        *outc = '&';
        return 5;
    }
    if (n >= 4 && strncmp(s, "&lt;", 4) == 0) {
        *outc = '<';
        return 4;
    }
    if (n >= 4 && strncmp(s, "&gt;", 4) == 0) {
        *outc = '>';
        return 4;
    }
    if (n >= 6 && strncmp(s, "&quot;", 6) == 0) {
        *outc = '"';
        return 6;
    }
    *outc = *s;
    return 1;
}

static int tag_is(const char *name, const char *want)
{
    return strcmp(name, want) == 0;
}

static const char *parse_tag(const char *s, const char *end, char *name, size_t nsz,
                             int *closing, char *href, size_t href_sz,
                             char *alt, size_t alt_sz, int *checked)
{
    const char *p = s;
    size_t ni = 0;

    *closing = 0;
    *checked = 0;
    name[0] = '\0';
    if (href && href_sz)
        href[0] = '\0';
    if (alt && alt_sz)
        alt[0] = '\0';
    if (p >= end || *p != '<')
        return s;
    p++;
    if (p < end && *p == '/') {
        *closing = 1;
        p++;
    }
    while (p < end && (isalnum((unsigned char)*p) || *p == '-')) {
        if (ni + 1 < nsz)
            name[ni++] = (char)tolower((unsigned char)*p);
        p++;
    }
    name[ni] = '\0';
    while (p < end && *p != '>') {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '/'))
            p++;
        if (p >= end || *p == '>')
            break;
        if (strncmp(p, "href=", 5) == 0 && href) {
            char q;
            p += 5;
            if (p < end && (*p == '"' || *p == '\'')) {
                q = *p++;
                ni = 0;
                while (p < end && *p != q) {
                    if (ni + 1 < href_sz)
                        href[ni++] = *p;
                    p++;
                }
                href[ni] = '\0';
                if (p < end)
                    p++;
            }
        } else if (strncmp(p, "alt=", 4) == 0 && alt) {
            char q;
            p += 4;
            if (p < end && (*p == '"' || *p == '\'')) {
                q = *p++;
                ni = 0;
                while (p < end && *p != q) {
                    if (ni + 1 < alt_sz)
                        alt[ni++] = *p;
                    p++;
                }
                alt[ni] = '\0';
                if (p < end)
                    p++;
            }
        } else if (strncmp(p, "checked", 7) == 0) {
            *checked = 1;
            p += 7;
        } else {
            while (p < end && *p != ' ' && *p != '>')
                p++;
        }
    }
    if (p < end && *p == '>')
        p++;
    return p;
}

static void open_tag(Conv *c, const char *name, const char *href, const char *alt, int checked)
{
    if (tag_is(name, "pre") || tag_is(name, "blockquote") ||
        tag_is(name, "ul") || tag_is(name, "ol") || tag_is(name, "table") ||
        (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0')) {
        if (!c->at_bol)
            conv_nl(c);
    }
    if (tag_is(name, "p") && !c->in_li && !c->at_bol)
        conv_nl(c);
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0') {
        c->heading = name[1] - '0';
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "strong")) {
        c->bold++;
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "em")) {
        c->em++;
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "del")) {
        c->del++;
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "code")) {
        c->code++;
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "pre")) {
        c->pre++;
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "blockquote")) {
        c->quote++;
        return;
    }
    if (tag_is(name, "ul") || tag_is(name, "ol")) {
        if (c->list_depth < 32) {
            c->list_ol[c->list_depth] = tag_is(name, "ol");
            c->list_n[c->list_depth] = 0;
            c->list_depth++;
        }
        return;
    }
    if (tag_is(name, "li")) {
        int i;
        int d = c->list_depth > 0 ? c->list_depth - 1 : 0;
        char tmp[32];
        if (!c->at_bol)
            conv_nl(c);
        conv_bol_prefix(c);
        for (i = 0; i < d; i++)
            buf_puts(&c->out, "  ");
        if (c->list_ol[d]) {
            c->list_n[d]++;
            snprintf(tmp, sizeof(tmp), "%d. ", c->list_n[d]);
            buf_puts(&c->out, tmp);
        } else {
            buf_puts(&c->out, "• ");
        }
        c->in_li++;
        c->skip_ws = 1;
        return;
    }
    if (tag_is(name, "br")) {
        conv_nl(c);
        return;
    }
    if (tag_is(name, "hr")) {
        int i;
        if (!c->at_bol)
            conv_nl(c);
        conv_bol_prefix(c);
        if (c->pal && c->pal->color) {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "\033[38;2;%u;%u;%um",
                     ch_r(c->pal->muted), ch_g(c->pal->muted), ch_b(c->pal->muted));
            buf_puts(&c->out, tmp);
        }
        for (i = 0; i < 40; i++)
            buf_puts(&c->out, "─");
        conv_nl(c);
        return;
    }
    if (tag_is(name, "a")) {
        c->in_a = 1;
        c->atext_len = 0;
        c->href_len = 0;
        if (href) {
            size_t n = strlen(href);
            if (n >= sizeof(c->href))
                n = sizeof(c->href) - 1;
            memcpy(c->href, href, n);
            c->href[n] = '\0';
            c->href_len = n;
        }
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "img")) {
        conv_bol_prefix(c);
        buf_puts(&c->out, "[");
        if (alt && alt[0])
            buf_puts(&c->out, alt);
        else
            buf_puts(&c->out, "image");
        buf_puts(&c->out, "]");
        return;
    }
    if (tag_is(name, "input")) {
        conv_bol_prefix(c);
        buf_puts(&c->out, checked ? "[x] " : "[ ] ");
        return;
    }
    if (tag_is(name, "th") || tag_is(name, "td")) {
        conv_bol_prefix(c);
        buf_puts(&c->out, "  ");
        return;
    }
    if (tag_is(name, "tr")) {
        if (!c->at_bol)
            conv_nl(c);
        return;
    }
}

static void close_tag(Conv *c, const char *name)
{
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0') {
        c->heading = 0;
        conv_nl(c);
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "p")) {
        if (!c->in_li)
            conv_nl(c);
        c->skip_ws = 0;
        return;
    }
    if (tag_is(name, "strong")) {
        if (c->bold > 0)
            c->bold--;
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "em")) {
        if (c->em > 0)
            c->em--;
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "del")) {
        if (c->del > 0)
            c->del--;
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "code")) {
        if (c->code > 0)
            c->code--;
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "pre")) {
        if (c->pre > 0)
            c->pre--;
        conv_nl(c);
        conv_sgr(c);
        return;
    }
    if (tag_is(name, "blockquote")) {
        if (c->quote > 0)
            c->quote--;
        if (!c->at_bol)
            conv_nl(c);
        return;
    }
    if (tag_is(name, "ul") || tag_is(name, "ol")) {
        if (c->list_depth > 0)
            c->list_depth--;
        if (!c->at_bol)
            conv_nl(c);
        return;
    }
    if (tag_is(name, "li")) {
        if (c->in_li > 0)
            c->in_li--;
        if (!c->at_bol)
            conv_nl(c);
        return;
    }
    if (tag_is(name, "a")) {
        c->in_a = 0;
        conv_sgr(c);
        if (c->href_len > 0) {
            int same = (c->atext_len == c->href_len &&
                        memcmp(c->atext, c->href, c->href_len) == 0);
            if (!same) {
                buf_puts(&c->out, " (");
                buf_put(&c->out, c->href, c->href_len);
                buf_puts(&c->out, ")");
            }
        }
        c->atext_len = 0;
        c->href_len = 0;
        return;
    }
    if (tag_is(name, "tr") || tag_is(name, "table")) {
        if (!c->at_bol)
            conv_nl(c);
        return;
    }
}

static char *html_to_term(const char *html, const TermPalette *pal)
{
    Conv c;
    const char *p = html;
    const char *end;
    char name[32], href[512], alt[256];
    int closing, checked;

    memset(&c, 0, sizeof(c));
    buf_init(&c.out);
    c.pal = pal;
    c.at_bol = 1;
    end = html + strlen(html);

    while (p < end) {
        if (*p == '&') {
            char ch;
            size_t used = decode_entity(p, (size_t)(end - p), &ch);
            conv_text(&c, &ch, 1);
            p += used;
            continue;
        }
        if (*p == '<') {
            const char *next = parse_tag(p, end, name, sizeof(name), &closing,
                                         href, sizeof(href), alt, sizeof(alt),
                                         &checked);
            if (name[0]) {
                if (closing)
                    close_tag(&c, name);
                else
                    open_tag(&c, name, href, alt, checked);
            }
            p = next;
            continue;
        }
        conv_text(&c, p, 1);
        p++;
    }
    if (pal && pal->color)
        buf_puts(&c.out, "\033[0m");
    if (!c.at_bol)
        buf_putc(&c.out, '\n');
    return buf_take(&c.out);
}

char *markdown_to_term(const char *md, size_t n, const TermPalette *pal)
{
    char *html = markdown_to_html(md, n);
    char *term;
    if (!html)
        return NULL;
    term = html_to_term(html, pal);
    free(html);
    return term;
}

/* ============================================================
 * Pager
 * ============================================================ */

static struct termios g_orig;
static int g_raw;
static volatile sig_atomic_t g_winch;
static volatile sig_atomic_t g_die;

static void on_winch(int sig)
{
    (void)sig;
    g_winch = 1;
}

static void on_die(int sig)
{
    (void)sig;
    g_die = 1;
}

static void term_restore(void)
{
    if (!g_raw)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
    write(STDOUT_FILENO, "\033[?1049l\033[?25h\033[0m", 19);
    g_raw = 0;
}

static int term_raw(void)
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &g_orig) < 0)
        return -1;
    t = g_orig;
    t.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0)
        return -1;
    write(STDOUT_FILENO, "\033[?1049h\033[?25l", 14);
    g_raw = 1;
    return 0;
}

static int vis_len(const char *s)
{
    int n = 0;
    while (*s) {
        if (s[0] == '\033' && s[1] == '[') {
            s += 2;
            while (*s && !(*s >= '@' && *s <= '~'))
                s++;
            if (*s)
                s++;
            continue;
        }
        n++;
        s++;
    }
    return n;
}

static void winsize(int *rows, int *cols)
{
    struct winsize w;
    *rows = 24;
    *cols = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        if (w.ws_row > 0)
            *rows = w.ws_row;
        if (w.ws_col > 0)
            *cols = w.ws_col;
    }
}

static int count_visual_rows(char **lines, int nlines, int cols)
{
    int i, total = 0;
    if (cols < 1)
        cols = 1;
    for (i = 0; i < nlines; i++) {
        int w = vis_len(lines[i]);
        if (w == 0)
            total += 1;
        else
            total += (w + cols - 1) / cols;
    }
    return total;
}

static int lines_equal(char **a, int na, char **b, int nb)
{
    int i;
    if (na != nb)
        return 0;
    for (i = 0; i < na; i++) {
        if (a[i] == b[i])
            continue;
        if (!a[i] || !b[i])
            return 0;
        if (strcmp(a[i], b[i]) != 0)
            return 0;
    }
    return 1;
}

/*
 * Draw one frame.  We do not clear the screen first — that is the
 * flicker.  Home, overwrite each row, erase-to-EOL, one write().
 * CSI 2026 (synchronized update) lets the terminal show the frame
 * only when it is complete; unknown private modes are ignored.
 */
static void paint(char **lines, int nlines, int top, const char *status)
{
    int rows, cols, i, vrow = 0, shown = 0;
    Buf b;
    char pos[16];

    winsize(&rows, &cols);
    if (rows < 2)
        rows = 2;
    if (cols < 1)
        cols = 1;

    buf_init(&b);
    buf_puts(&b, "\033[?2026h\033[H");
    for (i = 0; i < nlines && shown < rows - 1; i++) {
        int w = vis_len(lines[i] ? lines[i] : "");
        int chunks = w == 0 ? 1 : (w + cols - 1) / cols;
        int c;
        if (chunks < 1)
            chunks = 1;
        for (c = 0; c < chunks && shown < rows - 1; c++) {
            if (vrow++ < top)
                continue;
            /* Print the whole logical line on the first visible chunk;
             * the terminal wraps.  Good enough for SSH reading. */
            if (c == 0) {
                if (lines[i])
                    buf_puts(&b, lines[i]);
                buf_puts(&b, "\033[K\r\n");
            }
            shown++;
        }
    }
    while (shown < rows - 1) {
        buf_puts(&b, "\033[K\r\n");
        shown++;
    }
    /* Pin the bar to the last row so a wrap cannot scroll the screen. */
    snprintf(pos, sizeof(pos), "\033[%d;1H", rows);
    buf_puts(&b, pos);
    buf_puts(&b, "\033[7m");
    {
        int sl = (int)strlen(status);
        int pad;
        if (sl > cols)
            sl = cols;
        buf_put(&b, status, (size_t)sl);
        pad = cols - sl;
        while (pad-- > 0)
            buf_putc(&b, ' ');
    }
    buf_puts(&b, "\033[0m\033[K\033[?2026l");

    if (b.data && b.len > 0 && !b.oom)
        write(STDOUT_FILENO, b.data, b.len);
    free(b.data);
}

static char **split_lines(const char *text, int *out_n)
{
    int n = 0, i, cap;
    char **lines;
    const char *p, *start;

    for (p = text; *p; p++)
        if (*p == '\n')
            n++;
    if (text[0] && text[strlen(text) - 1] != '\n')
        n++;
    if (n < 1)
        n = 1;
    lines = calloc((size_t)n, sizeof(char *));
    if (!lines) {
        *out_n = 0;
        return NULL;
    }
    i = 0;
    start = text;
    cap = n;
    for (p = text; ; p++) {
        if (*p == '\n' || *p == '\0') {
            size_t len = (size_t)(p - start);
            if (len > 0 && start[len - 1] == '\r')
                len--;
            if (i < cap) {
                lines[i] = malloc(len + 1);
                if (lines[i]) {
                    memcpy(lines[i], start, len);
                    lines[i][len] = '\0';
                }
                i++;
            }
            start = p + 1;
            if (*p == '\0')
                break;
        }
    }
    *out_n = i;
    return lines;
}

static void free_lines(char **lines, int n)
{
    int i;
    if (!lines)
        return;
    for (i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
}

static time_t file_mtime(const char *path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0)
        return 0;
    return st.st_mtime;
}

static int pager(char **lines, int nlines, const char *watch_path,
                 const TermPalette *pal,
                 char *(*reread)(const char *path, size_t *n),
                 int follow)
{
    int top = 0;
    int rows, cols;
    int dirty = 1;
    time_t last_mtime = watch_path ? file_mtime(watch_path) : 0;
    struct sigaction sa_w, sa_d, old_int, old_term, old_hup, old_winch;

    if (term_raw() < 0)
        return -1;
    atexit(term_restore);

    memset(&sa_w, 0, sizeof(sa_w));
    sa_w.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa_w, &old_winch);
    memset(&sa_d, 0, sizeof(sa_d));
    sa_d.sa_handler = on_die;
    sigaction(SIGINT, &sa_d, &old_int);
    sigaction(SIGTERM, &sa_d, &old_term);
    sigaction(SIGHUP, &sa_d, &old_hup);

    for (;;) {
        char status[160];
        int vis, max_top;
        struct pollfd pfd;
        int pr;
        unsigned char ch;
        int old_top;

        if (g_die)
            break;
        if (g_winch) {
            g_winch = 0;
            dirty = 1;
        }

        if (dirty) {
            winsize(&rows, &cols);
            vis = count_visual_rows(lines, nlines, cols);
            max_top = vis - (rows - 1);
            if (max_top < 0)
                max_top = 0;
            if (top > max_top)
                top = max_top;
            if (top < 0)
                top = 0;

            if (watch_path)
                snprintf(status, sizeof(status),
                         " omamd  q quit  j/k scroll  g/G  f follow %s ",
                         follow ? "on" : "off");
            else
                snprintf(status, sizeof(status),
                         " omamd  q quit  j/k scroll  g/G top/end ");
            paint(lines, nlines, top, status);
            dirty = 0;
        }

        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pr = poll(&pfd, 1, watch_path ? 200 : -1);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (pr == 0 && watch_path && reread) {
            time_t now = file_mtime(watch_path);
            if (now != last_mtime) {
                size_t nn = 0;
                char *md = reread(watch_path, &nn);
                last_mtime = now;
                if (md) {
                    char *fresh = markdown_to_term(md, nn, pal);
                    free(md);
                    if (fresh) {
                        int nnl = 0;
                        char **nl = split_lines(fresh, &nnl);
                        free(fresh);
                        if (nl) {
                            if (lines_equal(lines, nlines, nl, nnl)) {
                                free_lines(nl, nnl);
                            } else {
                                free_lines(lines, nlines);
                                lines = nl;
                                nlines = nnl;
                                if (follow) {
                                    winsize(&rows, &cols);
                                    top = count_visual_rows(lines, nlines, cols) - (rows - 1);
                                    if (top < 0)
                                        top = 0;
                                }
                                dirty = 1;
                            }
                        }
                    }
                }
            }
            continue;
        }
        if (pr <= 0)
            continue;
        if (read(STDIN_FILENO, &ch, 1) != 1)
            break;
        if (ch == 'q' || ch == 'Q' || ch == 3)
            break;
        old_top = top;
        if (ch == 'j' || ch == '\n')
            top++;
        else if (ch == 'k')
            top--;
        else if (ch == ' ')
            top += rows > 2 ? rows - 2 : 1;
        else if (ch == 'b')
            top -= rows > 2 ? rows - 2 : 1;
        else if (ch == 'g')
            top = 0;
        else if (ch == 'G')
            top = 999999;
        else if ((ch == 'f' || ch == 'F') && watch_path) {
            follow = !follow;
            if (follow)
                top = 999999;
            dirty = 1;
        } else if (ch == '\033') {
            unsigned char seq[2];
            if (poll(&pfd, 1, 25) > 0 && read(STDIN_FILENO, seq, 1) == 1 && seq[0] == '[') {
                if (poll(&pfd, 1, 25) > 0 && read(STDIN_FILENO, seq + 1, 1) == 1) {
                    if (seq[1] == 'A')
                        top--;
                    else if (seq[1] == 'B')
                        top++;
                }
            }
        }
        if (top != old_top)
            dirty = 1;
    }

    term_restore();
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    sigaction(SIGHUP, &old_hup, NULL);
    sigaction(SIGWINCH, &old_winch, NULL);
    free_lines(lines, nlines);
    return 0;
}

int term_run(const char *watch_path, const char *md, size_t n,
             const TermPalette *pal,
             char *(*reread)(const char *path, size_t *n))
{
    char *text = markdown_to_term(md, n, pal);
    int nlines = 0;
    char **lines;

    if (!text)
        return 1;
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) {
        size_t wr = fwrite(text, 1, strlen(text), stdout);
        (void)wr;
        free(text);
        return 0;
    }
    lines = split_lines(text, &nlines);
    free(text);
    if (!lines)
        return 1;
    return pager(lines, nlines, watch_path, pal, reread,
                 watch_path ? 1 : 0) == 0 ? 0 : 1;
}
