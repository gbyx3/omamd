/*
 * markdown.c — turn Markdown text into HTML.
 *
 * How to read this file:
 *
 *   1. Section 1 (the buffer) is the C lesson.  Read it slowly.
 *      Almost everything else is "append some HTML into a buffer".
 *   2. Section 2 is tiny helpers that look at a single line.
 *   3. Section 3 walks *inside* a line: *italic*, `code`, links...
 *   4. Section 4 walks *between* lines: headings, lists, quotes...
 *   5. Section 5 is the one function the rest of the program calls.
 *
 * The strategy is "scan left to right, emit HTML as we go".  We do
 * not build a tree of nodes.  When we recognise a construct we write
 * the matching tags; when we do not, we copy the characters through
 * (with &, <, > escaped so they do not break the HTML).
 */

#include "markdown.h"

#include <ctype.h>   /* isalnum(), isdigit(), isspace() */
#include <stdio.h>   /* snprintf() — used to print a number into a string */
#include <stdlib.h>  /* malloc(), realloc(), free() */
#include <string.h>  /* memcpy(), memcmp(), memchr() */

/* ============================================================
 * Section 1: a growable string (the C you really need)
 * ============================================================
 *
 * HTML is built up a piece at a time: "<h1>", then the heading
 * text, then "</h1>", and so on.  We do not know the final size
 * up front, so we keep a *buffer*:
 *
 *   data     — pointer to a block of bytes we got from malloc()
 *   len      — how many of those bytes currently hold real content
 *   cap      — how many bytes we *own* (the capacity)
 *   oom      — "out of memory": set if realloc() ever fails
 *
 * When we need more room, we ask realloc() for a bigger block
 * (twice as big, so we do not do it on every character).
 *
 * `static` in front of a function/struct means "private to this
 * .c file".  main.c cannot call buf_putc.  That keeps the public
 * surface small: one function, markdown_to_html().
 */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int oom;
} Buf;

static void buf_init(Buf *b)
{
    /* `{0}` would also zero this, but spelling it out is clearer. */
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

    /* size_t is unsigned.  `b->len + extra` can wrap around to a
     * tiny number if both are huge.  The check below refuses that. */
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

    /* realloc(NULL, n) is the same as malloc(n).  On success it
     * returns a (possibly new) address; the old one is then invalid.
     * We only store the new address if it is not NULL. */
    grown = realloc(b->data, new_cap);
    if (!grown) {
        b->oom = 1;
        return 0;
    }
    b->data = grown;
    b->cap = new_cap;
    return 1;
}

static void buf_putc(Buf *b, char c)
{
    if (!buf_reserve(b, 1))
        return;
    b->data[b->len++] = c;
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
    /* C strings end with a 0 byte.  strlen() would also work;
     * walking until '\0' avoids an extra include-time dependency
     * on thinking about NUL vs length. */
    size_t n = 0;
    while (s[n] != '\0')
        n++;
    buf_put(b, s, n);
}

/* Copy text into HTML, replacing characters that would be read as
 * markup.  Without this, a Markdown file containing `<script>` or
 * `a < b` would break (or worse, inject tags). */
static void buf_escape(Buf *b, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        switch (s[i]) {
        case '&':  buf_puts(b, "&amp;");  break;
        case '<':  buf_puts(b, "&lt;");   break;
        case '>':  buf_puts(b, "&gt;");   break;
        case '"':  buf_puts(b, "&quot;"); break;
        default:   buf_putc(b, s[i]);     break;
        }
    }
}

/* Steal the finished string out of the buffer.  After this the Buf
 * is empty; the caller owns `data` and must free() it.  We add a
 * '\0' so the result is a normal C string. */
static char *buf_take(Buf *b)
{
    char *s;

    if (b->oom) {
        free(b->data);
        b->data = NULL;
        b->len = 0;
        b->cap = 0;
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

/* ============================================================
 * Section 2: looking at lines
 * ============================================================
 *
 * A "line" here is not its own copy of the text.  It is a *view*:
 * a pointer into the original Markdown plus a length.  That way
 * splitting a file into lines allocates one small array, not a
 * new string per line.
 */

typedef struct {
    const char *ptr;
    size_t len;
} Line;

static int is_blank_line(const Line *ln)
{
    size_t i;
    for (i = 0; i < ln->len; i++) {
        if (ln->ptr[i] != ' ' && ln->ptr[i] != '\t')
            return 0;
    }
    return 1;
}

/* How far in does the visible text start, counting a tab as
 * enough spaces to reach the next multiple of 4 (Markdown rules). */
static size_t line_indent(const Line *ln)
{
    size_t i;
    size_t col = 0;
    for (i = 0; i < ln->len; i++) {
        if (ln->ptr[i] == ' ')
            col++;
        else if (ln->ptr[i] == '\t')
            col += 4 - (col % 4);
        else
            break;
    }
    return col;
}

/* Skip `cols` indent columns and return the byte index where that
 * lands.  Used to peel indent off list-item continuation lines. */
static size_t skip_indent_cols(const Line *ln, size_t cols)
{
    size_t i = 0;
    size_t col = 0;
    while (i < ln->len && col < cols) {
        if (ln->ptr[i] == ' ') {
            col++;
            i++;
        } else if (ln->ptr[i] == '\t') {
            col += 4 - (col % 4);
            i++;
        } else {
            break;
        }
    }
    return i;
}

/* ATX heading: optional 0-3 spaces, then 1-6 `#`, then a space
 * (or end of line).  Returns heading level, or 0 if not a heading.
 * *text_off / *text_len describe the heading's inner text. */
static int atx_heading(const Line *ln, size_t *text_off, size_t *text_len)
{
    size_t i = skip_indent_cols(ln, 3);
    size_t hashes = 0;
    size_t end;

    if (i >= ln->len || ln->ptr[i] != '#')
        return 0;
    while (i < ln->len && ln->ptr[i] == '#' && hashes < 7) {
        hashes++;
        i++;
    }
    if (hashes < 1 || hashes > 6)
        return 0;
    if (i < ln->len && ln->ptr[i] != ' ' && ln->ptr[i] != '\t')
        return 0;
    while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
        i++;

    end = ln->len;
    while (end > i && (ln->ptr[end - 1] == ' ' || ln->ptr[end - 1] == '\t'))
        end--;
    /* Strip a closing run of '#' (the optional "###" on the right). */
    if (end > i && ln->ptr[end - 1] == '#') {
        size_t j = end;
        while (j > i && ln->ptr[j - 1] == '#')
            j--;
        while (j > i && (ln->ptr[j - 1] == ' ' || ln->ptr[j - 1] == '\t'))
            j--;
        end = j;
    }

    *text_off = i;
    *text_len = end - i;
    return (int)hashes;
}

/* Horizontal rule: three or more *, - or _ , only one kind,
 * spaces allowed in between. */
static int is_hr(const Line *ln)
{
    size_t i = skip_indent_cols(ln, 3);
    char marker;
    size_t count = 0;

    if (i >= ln->len)
        return 0;
    marker = ln->ptr[i];
    if (marker != '*' && marker != '-' && marker != '_')
        return 0;
    for (; i < ln->len; i++) {
        if (ln->ptr[i] == marker)
            count++;
        else if (ln->ptr[i] != ' ' && ln->ptr[i] != '\t')
            return 0;
    }
    return count >= 3;
}

/* Setext underline: a line of all '=' (h1) or all '-' (h2). */
static int setext_level(const Line *ln)
{
    size_t i = skip_indent_cols(ln, 3);
    char marker;
    size_t count = 0;

    if (i >= ln->len)
        return 0;
    marker = ln->ptr[i];
    if (marker != '=' && marker != '-')
        return 0;
    for (; i < ln->len; i++) {
        if (ln->ptr[i] == marker)
            count++;
        else if (ln->ptr[i] != ' ' && ln->ptr[i] != '\t')
            return 0;
    }
    if (count < 1)
        return 0;
    return marker == '=' ? 1 : 2;
}

/* Opening code fence: 3+ backticks or tildes.  *fence_char and
 * *fence_len describe it; *lang_off/len is the optional language. */
static int is_open_fence(const Line *ln, char *fence_char, size_t *fence_len,
                         size_t *lang_off, size_t *lang_len)
{
    size_t i = skip_indent_cols(ln, 3);
    char ch;
    size_t n = 0;
    size_t lang_start;
    size_t lang_end;

    if (i >= ln->len)
        return 0;
    ch = ln->ptr[i];
    if (ch != '`' && ch != '~')
        return 0;
    while (i < ln->len && ln->ptr[i] == ch) {
        n++;
        i++;
    }
    if (n < 3)
        return 0;

    while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
        i++;
    lang_start = i;
    while (i < ln->len && ln->ptr[i] != ' ' && ln->ptr[i] != '\t' && ln->ptr[i] != '`')
        i++;
    lang_end = i;

    *fence_char = ch;
    *fence_len = n;
    *lang_off = lang_start;
    *lang_len = lang_end - lang_start;
    return 1;
}

static int is_close_fence(const Line *ln, char fence_char, size_t fence_len)
{
    size_t i = skip_indent_cols(ln, 3);
    size_t n = 0;

    if (i >= ln->len || ln->ptr[i] != fence_char)
        return 0;
    while (i < ln->len && ln->ptr[i] == fence_char) {
        n++;
        i++;
    }
    if (n < fence_len)
        return 0;
    while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
        i++;
    return i == ln->len;
}

/* Quote prefix: 0-3 spaces then '>'.  Returns byte index after the
 * '>' and the optional following space, or (size_t)-1 if none. */
static size_t quote_prefix_len(const Line *ln)
{
    size_t i = skip_indent_cols(ln, 3);
    if (i >= ln->len || ln->ptr[i] != '>')
        return (size_t)-1;
    i++;
    if (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
        i++;
    return i;
}

/*
 * List marker at the start of a line.
 *
 *   indent      — column of the bullet / number
 *   width       — columns taken by indent + marker + the space after it
 *                 (this is the content indent of the item)
 *   ordered     — 1 for "1." / "1)", 0 for -, *, +
 *   start       — the number, for ordered lists
 *   content_off — byte index of the text after the marker (may be NULL)
 *
 * indent_max is how far in a marker is still allowed (3 at the
 * start of a block, because 4 spaces would be indented code).
 */
static int match_list_marker(const Line *ln, size_t indent_max,
                             size_t *indent, size_t *width,
                             int *ordered, int *start, size_t *content_off)
{
    size_t i = 0;
    size_t col = 0;
    size_t marker_col;
    int number = 0;
    int is_ordered = 0;

    while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t')) {
        if (ln->ptr[i] == ' ')
            col++;
        else
            col += 4 - (col % 4);
        i++;
    }
    if (col > indent_max)
        return 0;
    marker_col = col;
    if (i >= ln->len)
        return 0;

    if (ln->ptr[i] == '-' || ln->ptr[i] == '+' || ln->ptr[i] == '*') {
        i++;
        col++;
        is_ordered = 0;
    } else if (isdigit((unsigned char)ln->ptr[i])) {
        /* `(unsigned char)` is required: isdigit() is only defined
         * for 0..255 or EOF, and `char` might be signed. */
        while (i < ln->len && isdigit((unsigned char)ln->ptr[i]) && col - marker_col < 9) {
            number = number * 10 + (ln->ptr[i] - '0');
            i++;
            col++;
        }
        if (i >= ln->len || (ln->ptr[i] != '.' && ln->ptr[i] != ')'))
            return 0;
        i++;
        col++;
        is_ordered = 1;
    } else {
        return 0;
    }

    /* A marker must be followed by a space, or be the whole line. */
    if (i < ln->len) {
        if (ln->ptr[i] != ' ' && ln->ptr[i] != '\t')
            return 0;
        i++;
        col++;
    }

    *indent = marker_col;
    *width = col;
    *ordered = is_ordered;
    *start = is_ordered ? number : 1;
    if (content_off)
        *content_off = i;
    return 1;
}

static int is_table_sep_line(const Line *ln)
{
    size_t i = 0;
    int saw_cell = 0;
    int saw_dash = 0;

    while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
        i++;
    if (i < ln->len && ln->ptr[i] == '|')
        i++;

    while (i < ln->len) {
        saw_dash = 0;
        while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
            i++;
        if (i < ln->len && ln->ptr[i] == ':')
            i++;
        while (i < ln->len && ln->ptr[i] == '-') {
            saw_dash = 1;
            i++;
        }
        if (i < ln->len && ln->ptr[i] == ':')
            i++;
        while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
            i++;
        if (!saw_dash)
            return 0;
        saw_cell = 1;
        if (i >= ln->len)
            break;
        if (ln->ptr[i] != '|')
            return 0;
        i++;
        while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
            i++;
        if (i == ln->len)
            break;
    }
    return saw_cell;
}

static int looks_like_table_row(const Line *ln)
{
    return memchr(ln->ptr, '|', ln->len) != NULL;
}

/* ============================================================
 * Section 3: inline markup (bold, links, ...)
 * ============================================================ */

static int is_word_char(char c)
{
    return isalnum((unsigned char)c) || (unsigned char)c >= 0x80;
}

static size_t run_length(const char *s, size_t n, size_t i, char c)
{
    size_t k = i;
    while (k < n && s[k] == c)
        k++;
    return k - i;
}

static size_t find_closing_run(const char *s, size_t n, size_t from,
                               char marker, size_t want)
{
    size_t i = from;
    while (i < n) {
        if (s[i] == '\\' && i + 1 < n) {
            i += 2;
            continue;
        }
        if (s[i] == marker && run_length(s, n, i, marker) >= want) {
            if (marker == '_') {
                /* Do not close _italic_ in the middle of a word. */
                size_t after = i + want;
                if (after < n && is_word_char(s[after])) {
                    i++;
                    continue;
                }
            }
            return i;
        }
        i++;
    }
    return (size_t)-1;
}

static size_t find_closing_ticks(const char *s, size_t n, size_t from, size_t ticks)
{
    size_t i = from;
    while (i < n) {
        if (s[i] == '`') {
            size_t got = run_length(s, n, i, '`');
            if (got == ticks)
                return i;
            i += got;
        } else {
            i++;
        }
    }
    return (size_t)-1;
}

/* Find the ']' that ends a [link text] or ![alt], skipping nested
 * brackets one level and honouring backslash escapes. */
static size_t find_link_text_end(const char *s, size_t n, size_t from)
{
    size_t i;
    int depth = 1;
    for (i = from; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            continue;
        }
        if (s[i] == '[')
            depth++;
        else if (s[i] == ']') {
            depth--;
            if (depth == 0)
                return i;
        }
    }
    return (size_t)-1;
}

static size_t find_matching_paren(const char *s, size_t n, size_t from)
{
    size_t i;
    int depth = 1;
    for (i = from; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            continue;
        }
        if (s[i] == '(')
            depth++;
        else if (s[i] == ')') {
            depth--;
            if (depth == 0)
                return i;
        }
    }
    return (size_t)-1;
}

static int url_end_char(char c)
{
    /* Punctuation we do not want glued onto a bare URL. */
    return c == ' ' || c == '\t' || c == '<' || c == '>' ||
           c == '"' || c == '\'' || c == ')';
}

/* A URL has a scheme if it starts with `name:` where name is letters
 * (then letters, digits, +, ., -).  `foo.md` has none; `javascript:`
 * and `https:` do. */
static int url_has_scheme(const char *s, size_t n)
{
    size_t i;
    if (n == 0 || !isalpha((unsigned char)s[0]))
        return 0;
    for (i = 1; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (s[i] == ':')
            return 1;
        if (!(isalnum(c) || c == '+' || c == '.' || c == '-'))
            return 0;
    }
    return 0;
}

static int url_starts_scheme(const char *s, size_t n, const char *scheme)
{
    size_t i;
    for (i = 0; scheme[i] != '\0'; i++) {
        if (i >= n)
            return 0;
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)scheme[i]))
            return 0;
    }
    return i < n && s[i] == ':';
}

/* Links in the HTML we emit.  http(s) and mailto are fine; relative
 * paths (notes.md, ./img) are fine.  javascript:, data:, file:, and
 * protocol-relative //host are not.  A leading / would resolve to the
 * filesystem root against a file:// base URI, so we reject that too. */
static int url_is_safe_href(const char *s, size_t n)
{
    if (n == 0)
        return 0;
    if (!url_has_scheme(s, n)) {
        if (s[0] == '/')
            return 0;
        return 1;
    }
    return url_starts_scheme(s, n, "https") ||
           url_starts_scheme(s, n, "http") ||
           url_starts_scheme(s, n, "mailto");
}

/* Images: same relative rules, plus http(s) and data:image/...
 * (never data:text/html). */
static int url_is_safe_img(const char *s, size_t n)
{
    size_t i;
    if (n == 0)
        return 0;
    if (!url_has_scheme(s, n)) {
        if (s[0] == '/')
            return 0;
        return 1;
    }
    if (url_starts_scheme(s, n, "https") || url_starts_scheme(s, n, "http"))
        return 1;
    if (!url_starts_scheme(s, n, "data"))
        return 0;
    i = 5; /* past "data:" */
    if (i + 6 > n)
        return 0;
    return tolower((unsigned char)s[i]) == 'i' &&
           tolower((unsigned char)s[i + 1]) == 'm' &&
           tolower((unsigned char)s[i + 2]) == 'a' &&
           tolower((unsigned char)s[i + 3]) == 'g' &&
           tolower((unsigned char)s[i + 4]) == 'e' &&
           s[i + 5] == '/';
}

/* Forward declaration: render_inline calls itself for the insides
 * of links and emphasis.  In C a function must be declared before
 * it is used; this line is that declaration. */
static void render_inline(Buf *out, const char *s, size_t n, int depth);

static void emit_link_or_image(Buf *out, const char *s, size_t n,
                               size_t *i, int is_image, int depth)
{
    size_t text_beg = *i + (is_image ? 2 : 1);
    size_t text_end;
    size_t url_beg;
    size_t url_end;
    size_t p;

    text_end = find_link_text_end(s, n, text_beg);
    if (text_end == (size_t)-1)
        goto literal;

    p = text_end + 1;
    if (p >= n || s[p] != '(')
        goto literal;
    p++;
    while (p < n && (s[p] == ' ' || s[p] == '\t'))
        p++;

    if (p < n && s[p] == '<') {
        url_beg = p + 1;
        url_end = url_beg;
        while (url_end < n && s[url_end] != '>' && s[url_end] != '\n')
            url_end++;
        if (url_end >= n || s[url_end] != '>')
            goto literal;
        p = url_end + 1;
    } else {
        url_beg = p;
        url_end = find_matching_paren(s, n, p);
        if (url_end == (size_t)-1)
            goto literal;
        /* The URL stops before optional "title" in quotes. */
        {
            size_t u = url_beg;
            size_t url_stop = url_end;
            while (u < url_end && s[u] != ' ' && s[u] != '\t')
                u++;
            if (u < url_end)
                url_stop = u;
            p = url_end + 1;
            url_end = url_stop;
        }
    }

    if (is_image) {
        if (!url_is_safe_img(s + url_beg, url_end - url_beg)) {
            buf_escape(out, s + text_beg, text_end - text_beg);
            *i = p;
            return;
        }
        buf_puts(out, "<img src=\"");
        buf_escape(out, s + url_beg, url_end - url_beg);
        buf_puts(out, "\" alt=\"");
        buf_escape(out, s + text_beg, text_end - text_beg);
        buf_puts(out, "\">");
    } else {
        if (!url_is_safe_href(s + url_beg, url_end - url_beg)) {
            render_inline(out, s + text_beg, text_end - text_beg, depth + 1);
            *i = p;
            return;
        }
        buf_puts(out, "<a href=\"");
        buf_escape(out, s + url_beg, url_end - url_beg);
        buf_puts(out, "\">");
        render_inline(out, s + text_beg, text_end - text_beg, depth + 1);
        buf_puts(out, "</a>");
    }
    *i = p;
    return;

literal:
    buf_escape(out, s + *i, 1);
    *i += 1;
}

static void render_inline(Buf *out, const char *s, size_t n, int depth)
{
    size_t i = 0;

    /* A hard cap so a file full of ********** cannot crash us
     * by recursing forever. */
    if (depth > 32) {
        buf_escape(out, s, n);
        return;
    }

    while (i < n) {
        char c = s[i];

        /* Backslash: the next character is just a character. */
        if (c == '\\' && i + 1 < n) {
            buf_escape(out, s + i + 1, 1);
            i += 2;
            continue;
        }

        /* Inline `code`.  One or more backticks, closed by the
         * same number.  That is how you write a backtick *in* code. */
        if (c == '`') {
            size_t ticks = run_length(s, n, i, '`');
            size_t close = find_closing_ticks(s, n, i + ticks, ticks);
            if (close != (size_t)-1) {
                size_t a = i + ticks;
                size_t b = close;
                /* CommonMark: one optional space on each side. */
                if (b > a && s[a] == ' ' && s[b - 1] == ' ' && b - a >= 2) {
                    a++;
                    b--;
                }
                buf_puts(out, "<code>");
                buf_escape(out, s + a, b - a);
                buf_puts(out, "</code>");
                i = close + ticks;
                continue;
            }
        }

        /* Images then links.  Images start with ![ so check '!' first. */
        if (c == '!' && i + 1 < n && s[i + 1] == '[') {
            emit_link_or_image(out, s, n, &i, 1, depth);
            continue;
        }
        if (c == '[') {
            emit_link_or_image(out, s, n, &i, 0, depth);
            continue;
        }

        /* ~~strikethrough~~ (GitHub-style). */
        if (c == '~' && i + 1 < n && s[i + 1] == '~') {
            size_t close = find_closing_run(s, n, i + 2, '~', 2);
            if (close != (size_t)-1 && close > i + 2) {
                buf_puts(out, "<del>");
                render_inline(out, s + i + 2, close - (i + 2), depth + 1);
                buf_puts(out, "</del>");
                i = close + 2;
                continue;
            }
        }

        /* Emphasis: *, **, ***, and the underscore forms.
         * Underscores are ignored in the middle of a word so
         * file_name does not become file<em>name. */
        if (c == '*' || c == '_') {
            size_t open_len = run_length(s, n, i, c);
            int allowed = 1;
            if (c == '_' && i > 0 && is_word_char(s[i - 1]))
                allowed = 0;
            if (allowed && open_len >= 1) {
                size_t want;
                for (want = open_len > 3 ? 3 : open_len; want >= 1; want--) {
                    size_t close;
                    if (c == '_' && i > 0 && is_word_char(s[i - 1]))
                        continue;
                    close = find_closing_run(s, n, i + want, c, want);
                    if (close != (size_t)-1 && close > i + want) {
                        const char *open_tag;
                        const char *close_tag;
                        if (want == 3) {
                            open_tag = "<strong><em>";
                            close_tag = "</em></strong>";
                        } else if (want == 2) {
                            open_tag = "<strong>";
                            close_tag = "</strong>";
                        } else {
                            open_tag = "<em>";
                            close_tag = "</em>";
                        }
                        buf_puts(out, open_tag);
                        render_inline(out, s + i + want, close - (i + want), depth + 1);
                        buf_puts(out, close_tag);
                        i = close + want;
                        goto next_inline;
                    }
                }
            }
        }

        /* <http://...> autolink, or pass through a simple HTML tag. */
        if (c == '<') {
            size_t j = i + 1;
            if (j + 7 <= n && (memcmp(s + j, "http://", 7) == 0 ||
                               (j + 8 <= n && memcmp(s + j, "https://", 8) == 0) ||
                               (j + 7 <= n && memcmp(s + j, "mailto:", 7) == 0))) {
                while (j < n && s[j] != '>' && s[j] != ' ' && s[j] != '\n')
                    j++;
                if (j < n && s[j] == '>') {
                    buf_puts(out, "<a href=\"");
                    buf_escape(out, s + i + 1, j - (i + 1));
                    buf_puts(out, "\">");
                    buf_escape(out, s + i + 1, j - (i + 1));
                    buf_puts(out, "</a>");
                    i = j + 1;
                    continue;
                }
            }
        }

        /* Bare http(s) URLs. */
        if ((c == 'h' || c == 'H') &&
            ((i + 8 <= n && strncmp(s + i, "https://", 8) == 0) ||
             (i + 7 <= n && strncmp(s + i, "http://", 7) == 0) ||
             (i + 8 <= n && strncmp(s + i, "HTTPS://", 8) == 0) ||
             (i + 7 <= n && strncmp(s + i, "HTTP://", 7) == 0))) {
            size_t j = i;
            while (j < n && !url_end_char(s[j]) && s[j] != '\n')
                j++;
            while (j > i && (s[j - 1] == '.' || s[j - 1] == ',' ||
                             s[j - 1] == ';' || s[j - 1] == ':' ||
                             s[j - 1] == '!'))
                j--;
            buf_puts(out, "<a href=\"");
            buf_escape(out, s + i, j - i);
            buf_puts(out, "\">");
            buf_escape(out, s + i, j - i);
            buf_puts(out, "</a>");
            i = j;
            continue;
        }

        buf_escape(out, s + i, 1);
        i++;
next_inline:
        ;
    }
}

/* ============================================================
 * Section 4: block structure (headings, lists, quotes, ...)
 * ============================================================
 *
 * parse_blocks walks an array of Line views from index `i` up to
 * `end` and appends HTML.  It returns the index it stopped at.
 *
 * Containers (quotes, list items) *strip their prefix* into a
 * temporary Line array and call parse_blocks again on that.  So a
 * list item that contains a quote that contains a list is just
 * recursion — the same function, a smaller slice of lines.
 *
 * Block nesting (quotes in lists in quotes...) is capped so a
 * malicious file cannot blow the C stack.
 */

#define OMAMD_MAX_BLOCK_DEPTH 32

static size_t parse_blocks(Buf *out, Line *lines, size_t i, size_t end, int depth);

static int line_starts_block(const Line *ln)
{
    size_t dummy_a, dummy_b, dummy_c;
    char dummy_ch;
    size_t indent, width;
    int ordered, start;

    if (is_blank_line(ln))
        return 1;
    if (atx_heading(ln, &dummy_a, &dummy_b))
        return 1;
    if (is_hr(ln))
        return 1;
    if (is_open_fence(ln, &dummy_ch, &dummy_a, &dummy_b, &dummy_c))
        return 1;
    if (quote_prefix_len(ln) != (size_t)-1)
        return 1;
    if (match_list_marker(ln, 3, &indent, &width, &ordered, &start, NULL))
        return 1;
    if (line_indent(ln) >= 4)
        return 1;
    return 0;
}

static void emit_heading(Buf *out, int level, const char *s, size_t n)
{
    char tag[8];
    /* snprintf writes a C string into a small stack array.
     * The 8 is the size of `tag` so it cannot overflow. */
    snprintf(tag, sizeof(tag), "<h%d>", level);
    buf_puts(out, tag);
    render_inline(out, s, n, 0);
    snprintf(tag, sizeof(tag), "</h%d>", level);
    buf_puts(out, tag);
    buf_putc(out, '\n');
}

static size_t parse_fence(Buf *out, Line *lines, size_t i, size_t end)
{
    char fence_char;
    size_t fence_len, lang_off, lang_len;
    size_t k;

    if (!is_open_fence(&lines[i], &fence_char, &fence_len, &lang_off, &lang_len))
        return i;

    buf_puts(out, "<pre><code");
    if (lang_len > 0) {
        buf_puts(out, " class=\"language-");
        buf_escape(out, lines[i].ptr + lang_off, lang_len);
        buf_puts(out, "\"");
    }
    buf_puts(out, ">");

    for (k = i + 1; k < end; k++) {
        if (is_close_fence(&lines[k], fence_char, fence_len)) {
            buf_puts(out, "</code></pre>\n");
            return k + 1;
        }
        buf_escape(out, lines[k].ptr, lines[k].len);
        buf_putc(out, '\n');
    }
    /* Unclosed fence: eat the rest of the document as code. */
    buf_puts(out, "</code></pre>\n");
    return end;
}

static size_t parse_indented_code(Buf *out, Line *lines, size_t i, size_t end)
{
    size_t k;

    if (line_indent(&lines[i]) < 4 || is_blank_line(&lines[i]))
        return i;

    buf_puts(out, "<pre><code>");
    k = i;
    while (k < end) {
        if (is_blank_line(&lines[k])) {
            /* Peek: more indented code after the blank, or are we done? */
            size_t look = k + 1;
            while (look < end && is_blank_line(&lines[look]))
                look++;
            if (look < end && line_indent(&lines[look]) >= 4) {
                buf_putc(out, '\n');
                k++;
                continue;
            }
            break;
        }
        if (line_indent(&lines[k]) < 4)
            break;
        {
            size_t off = skip_indent_cols(&lines[k], 4);
            buf_escape(out, lines[k].ptr + off, lines[k].len - off);
            buf_putc(out, '\n');
        }
        k++;
    }
    buf_puts(out, "</code></pre>\n");
    return k;
}

static size_t parse_quote(Buf *out, Line *lines, size_t i, size_t end, int depth)
{
    Line *inner;
    size_t count = 0;
    size_t k;
    size_t consumed;

    if (quote_prefix_len(&lines[i]) == (size_t)-1)
        return i;

    /* Count how many consecutive quote (or blank-inside-quote) lines. */
    for (k = i; k < end; k++) {
        if (quote_prefix_len(&lines[k]) != (size_t)-1)
            count++;
        else if (is_blank_line(&lines[k]))
            break;
        else
            break;
    }

    inner = malloc(count * sizeof(Line));
    if (!inner) {
        out->oom = 1;
        return end;
    }
    for (k = 0; k < count; k++) {
        size_t pref = quote_prefix_len(&lines[i + k]);
        if (pref == (size_t)-1) {
            inner[k].ptr = lines[i + k].ptr;
            inner[k].len = 0;
        } else {
            inner[k].ptr = lines[i + k].ptr + pref;
            inner[k].len = lines[i + k].len - pref;
        }
    }

    buf_puts(out, "<blockquote>\n");
    parse_blocks(out, inner, 0, count, depth + 1);
    buf_puts(out, "</blockquote>\n");
    free(inner);

    consumed = i + count;
    return consumed;
}

static size_t count_table_cols(const Line *ln)
{
    size_t i = 0;
    size_t cols = 0;
    int in_cell = 0;

    while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
        i++;
    if (i < ln->len && ln->ptr[i] == '|')
        i++;
    while (i < ln->len) {
        if (ln->ptr[i] == '|') {
            cols++;
            in_cell = 0;
            i++;
        } else {
            in_cell = 1;
            i++;
        }
    }
    if (in_cell)
        cols++;
    return cols;
}

static void emit_table_row(Buf *out, const Line *ln, int is_header,
                           const char *aligns, size_t ncols)
{
    size_t i = 0;
    size_t col = 0;
    size_t cell_start;
    const char *tag = is_header ? "th" : "td";

    while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
        i++;
    if (i < ln->len && ln->ptr[i] == '|')
        i++;

    buf_puts(out, "<tr>");
    while (col < ncols) {
        cell_start = i;
        while (i < ln->len && ln->ptr[i] != '|')
            i++;
        {
            size_t a = cell_start;
            size_t b = i;
            while (a < b && (ln->ptr[a] == ' ' || ln->ptr[a] == '\t'))
                a++;
            while (b > a && (ln->ptr[b - 1] == ' ' || ln->ptr[b - 1] == '\t'))
                b--;
            buf_putc(out, '<');
            buf_puts(out, tag);
            if (aligns && col < ncols && aligns[col] != 'l') {
                buf_puts(out, " style=\"text-align:");
                buf_puts(out, aligns[col] == 'c' ? "center" : "right");
                buf_puts(out, "\"");
            }
            buf_putc(out, '>');
            render_inline(out, ln->ptr + a, b - a, 0);
            buf_puts(out, "</");
            buf_puts(out, tag);
            buf_puts(out, ">");
        }
        col++;
        if (i < ln->len && ln->ptr[i] == '|')
            i++;
        else if (i >= ln->len)
            break;
    }
    buf_puts(out, "</tr>\n");
}

static void parse_table_aligns(const Line *ln, char *aligns, size_t ncols)
{
    size_t i = 0;
    size_t col = 0;

    while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
        i++;
    if (i < ln->len && ln->ptr[i] == '|')
        i++;

    while (col < ncols && i < ln->len) {
        int left = 0;
        int right = 0;
        while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
            i++;
        if (i < ln->len && ln->ptr[i] == ':') {
            left = 1;
            i++;
        }
        while (i < ln->len && ln->ptr[i] == '-')
            i++;
        if (i < ln->len && ln->ptr[i] == ':') {
            right = 1;
            i++;
        }
        if (left && right)
            aligns[col] = 'c';
        else if (right)
            aligns[col] = 'r';
        else
            aligns[col] = 'l';
        col++;
        while (i < ln->len && ln->ptr[i] != '|')
            i++;
        if (i < ln->len && ln->ptr[i] == '|')
            i++;
    }
    while (col < ncols) {
        aligns[col] = 'l';
        col++;
    }
}

static size_t parse_table(Buf *out, Line *lines, size_t i, size_t end)
{
    size_t ncols;
    char *aligns;
    size_t k;

    if (i + 1 >= end)
        return i;
    if (!looks_like_table_row(&lines[i]) || !is_table_sep_line(&lines[i + 1]))
        return i;

    ncols = count_table_cols(&lines[i]);
    if (ncols == 0)
        return i;
    aligns = malloc(ncols);
    if (!aligns) {
        out->oom = 1;
        return end;
    }
    parse_table_aligns(&lines[i + 1], aligns, ncols);

    buf_puts(out, "<table>\n<thead>\n");
    emit_table_row(out, &lines[i], 1, aligns, ncols);
    buf_puts(out, "</thead>\n<tbody>\n");

    k = i + 2;
    while (k < end && looks_like_table_row(&lines[k]) &&
           !is_blank_line(&lines[k]) && !is_hr(&lines[k])) {
        emit_table_row(out, &lines[k], 0, aligns, ncols);
        k++;
    }
    buf_puts(out, "</tbody>\n</table>\n");
    free(aligns);
    return k;
}

static int strip_task_box(Line *ln, int *checked)
{
    size_t i = 0;
    while (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
        i++;
    if (i + 3 <= ln->len && ln->ptr[i] == '[' && ln->ptr[i + 2] == ']' &&
        (ln->ptr[i + 1] == ' ' || ln->ptr[i + 1] == 'x' || ln->ptr[i + 1] == 'X')) {
        *checked = (ln->ptr[i + 1] != ' ');
        i += 3;
        if (i < ln->len && (ln->ptr[i] == ' ' || ln->ptr[i] == '\t'))
            i++;
        ln->ptr += i;
        ln->len -= i;
        return 1;
    }
    return 0;
}

static size_t parse_list(Buf *out, Line *lines, size_t i, size_t end, int depth)
{
    size_t indent, width;
    int ordered, start;
    int list_ordered;
    size_t list_indent;
    char num[32];

    if (!match_list_marker(&lines[i], 3, &indent, &width, &ordered, &start, NULL))
        return i;

    list_ordered = ordered;
    list_indent = indent;

    if (list_ordered) {
        buf_puts(out, "<ol");
        if (start != 1) {
            buf_puts(out, " start=\"");
            snprintf(num, sizeof(num), "%d", start);
            buf_puts(out, num);
            buf_puts(out, "\"");
        }
        buf_puts(out, ">\n");
    } else {
        buf_puts(out, "<ul>\n");
    }

    while (i < end) {
        size_t item_indent, item_width, content_off;
        int item_ordered, item_start;
        Line *inner;
        size_t cap;
        size_t ninner = 0;
        int checked = 0;
        int is_task = 0;
        Line first;

        if (!match_list_marker(&lines[i], list_indent, &item_indent, &item_width,
                               &item_ordered, &item_start, &content_off))
            break;
        if (item_indent != list_indent || item_ordered != list_ordered)
            break;

        /* First line of the item: everything after "- " / "1. ".
         * `content_off` is a byte index, not a column count — using
         * indent-skip here used to leave the marker in place and
         * parse_list would call itself forever. */
        if (content_off > lines[i].len)
            content_off = lines[i].len;
        first.ptr = lines[i].ptr + content_off;
        first.len = lines[i].len - content_off;
        is_task = strip_task_box(&first, &checked);

        cap = (end - i) + 1;
        inner = malloc(cap * sizeof(Line));
        if (!inner) {
            out->oom = 1;
            return end;
        }
        inner[ninner++] = first;
        i++;

        /* Continuation lines: indented at least to the content
         * column, plus blank lines that still belong to the item. */
        while (i < end) {
            size_t next_indent, next_width;
            int next_ordered, next_start;

            if (is_blank_line(&lines[i])) {
                size_t look = i + 1;
                while (look < end && is_blank_line(&lines[look]))
                    look++;
                if (look < end &&
                    match_list_marker(&lines[look], list_indent, &next_indent,
                                      &next_width, &next_ordered, &next_start,
                                      NULL) &&
                    next_indent == list_indent) {
                    break; /* blank line between sibling items */
                }
                if (look >= end)
                    break;
                inner[ninner].ptr = "";
                inner[ninner].len = 0;
                ninner++;
                i++;
                continue;
            }

            if (match_list_marker(&lines[i], list_indent, &next_indent,
                                  &next_width, &next_ordered, &next_start,
                                  NULL) &&
                next_indent == list_indent) {
                break; /* next sibling */
            }

            if (line_indent(&lines[i]) >= item_width) {
                size_t off = skip_indent_cols(&lines[i], item_width);
                inner[ninner].ptr = lines[i].ptr + off;
                inner[ninner].len = lines[i].len - off;
                ninner++;
                i++;
                continue;
            }
            break;
        }

        /* Drop trailing blank inner lines so we do not wrap an extra <p>. */
        while (ninner > 0 && inner[ninner - 1].len == 0)
            ninner--;

        buf_puts(out, "<li>");
        if (is_task) {
            buf_puts(out, "<input type=\"checkbox\" disabled");
            if (checked)
                buf_puts(out, " checked");
            buf_puts(out, "> ");
        }
        parse_blocks(out, inner, 0, ninner, depth + 1);
        buf_puts(out, "</li>\n");
        free(inner);
    }

    buf_puts(out, list_ordered ? "</ol>\n" : "</ul>\n");
    return i;
}

static size_t parse_paragraph(Buf *out, Line *lines, size_t i, size_t end)
{
    size_t start = i;
    size_t k;
    Buf tmp;
    int level;

    while (i < end && !is_blank_line(&lines[i]) && !line_starts_block(&lines[i])) {
        /* `===` is a setext underline but not an HR, so it does not
         * count as a new block.  Stop the paragraph so we can turn
         * the previous lines into an <h1>. */
        if (i > start && setext_level(&lines[i]))
            break;
        /* A table can interrupt a paragraph. */
        if (i + 1 < end && looks_like_table_row(&lines[i]) &&
            is_table_sep_line(&lines[i + 1]))
            break;
        i++;
    }

    /* A `---` line is both an HR *and* a setext <h2> underline.
     * parse_blocks treats a lone --- as <hr>.  If we already collected
     * paragraph text, the same line is a heading underline instead. */
    if (i < end && i > start && setext_level(&lines[i])) {
        level = setext_level(&lines[i]);
        buf_init(&tmp);
        for (k = start; k < i; k++) {
            if (k > start)
                buf_putc(&tmp, ' ');
            buf_put(&tmp, lines[k].ptr, lines[k].len);
        }
        emit_heading(out, level, tmp.data ? tmp.data : "", tmp.len);
        free(tmp.data);
        return i + 1;
    }

    if (i == start)
        return i;

    buf_puts(out, "<p>");
    for (k = start; k < i; k++) {
        size_t len = lines[k].len;
        int hard_break = 0;

        if (len >= 2 && lines[k].ptr[len - 1] == ' ' && lines[k].ptr[len - 2] == ' ') {
            while (len > 0 && lines[k].ptr[len - 1] == ' ')
                len--;
            hard_break = 1;
        }
        render_inline(out, lines[k].ptr, len, 0);
        if (k + 1 < i) {
            if (hard_break)
                buf_puts(out, "<br>\n");
            else
                buf_putc(out, '\n');
        }
    }
    buf_puts(out, "</p>\n");
    return i;
}

static size_t parse_blocks(Buf *out, Line *lines, size_t i, size_t end, int depth)
{
    while (i < end) {
        size_t next;
        size_t text_off, text_len;
        int level;
        size_t dummy_a, dummy_b, dummy_c;
        char dummy_ch;
        size_t indent, width;
        int ordered, start;

        if (is_blank_line(&lines[i])) {
            i++;
            continue;
        }

        level = atx_heading(&lines[i], &text_off, &text_len);
        if (level) {
            emit_heading(out, level, lines[i].ptr + text_off, text_len);
            i++;
            continue;
        }

        if (is_open_fence(&lines[i], &dummy_ch, &dummy_a, &dummy_b, &dummy_c)) {
            i = parse_fence(out, lines, i, end);
            continue;
        }

        if (is_hr(&lines[i])) {
            buf_puts(out, "<hr>\n");
            i++;
            continue;
        }

        if (depth < OMAMD_MAX_BLOCK_DEPTH &&
            quote_prefix_len(&lines[i]) != (size_t)-1) {
            i = parse_quote(out, lines, i, end, depth);
            continue;
        }

        if (i + 1 < end && looks_like_table_row(&lines[i]) &&
            is_table_sep_line(&lines[i + 1])) {
            i = parse_table(out, lines, i, end);
            continue;
        }

        if (depth < OMAMD_MAX_BLOCK_DEPTH &&
            match_list_marker(&lines[i], 3, &indent, &width, &ordered, &start, NULL)) {
            i = parse_list(out, lines, i, end, depth);
            continue;
        }

        if (line_indent(&lines[i]) >= 4) {
            i = parse_indented_code(out, lines, i, end);
            continue;
        }

        next = parse_paragraph(out, lines, i, end);
        if (next == i) {
            /* Should not happen, but never spin. */
            i++;
        } else {
            i = next;
        }
    }
    return i;
}

/* Split the whole document into Line views.  We count lines first
 * so we can malloc exactly once.  A trailing newline does not create
 * an extra empty line ("a\n" is one line, "a\n\n" is two). */
static Line *split_lines(const char *s, size_t n, size_t *out_count)
{
    size_t i;
    size_t count = 0;
    size_t start;
    size_t idx;
    Line *lines;

    if (n == 0) {
        *out_count = 0;
        return malloc(sizeof(Line)); /* non-NULL so the caller can free it */
    }

    start = 0;
    for (i = 0; i < n; i++) {
        if (s[i] == '\n') {
            count++;
            start = i + 1;
        }
    }
    if (start < n)
        count++;

    lines = malloc(count * sizeof(Line));
    if (!lines) {
        *out_count = 0;
        return NULL;
    }

    start = 0;
    idx = 0;
    for (i = 0; i < n; i++) {
        if (s[i] == '\n') {
            size_t len = i - start;
            if (len > 0 && s[start + len - 1] == '\r')
                len--;
            lines[idx].ptr = s + start;
            lines[idx].len = len;
            idx++;
            start = i + 1;
        }
    }
    if (start < n) {
        size_t len = n - start;
        if (len > 0 && s[start + len - 1] == '\r')
            len--;
        lines[idx].ptr = s + start;
        lines[idx].len = len;
        idx++;
    }
    *out_count = idx;
    return lines;
}

/* ============================================================
 * Section 5: the one public function
 * ============================================================ */

char *markdown_to_html(const char *md, size_t n)
{
    Line *lines;
    size_t nlines;
    Buf out;

    /* Skip a UTF-8 BOM if a Windows editor stuck one on the file. */
    if (n >= 3 &&
        (unsigned char)md[0] == 0xEF &&
        (unsigned char)md[1] == 0xBB &&
        (unsigned char)md[2] == 0xBF) {
        md += 3;
        n -= 3;
    }

    lines = split_lines(md, n, &nlines);
    if (!lines)
        return NULL;

    buf_init(&out);
    parse_blocks(&out, lines, 0, nlines, 0);
    free(lines);
    return buf_take(&out);
}
