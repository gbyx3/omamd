#define _DEFAULT_SOURCE

/*
 * util.c — read a file or stdin, a few path helpers.
 *
 * fopen / fread / fclose is the classic C way to read a file.
 * GTK also has g_file_get_contents(); we use the C version so the
 * CLI can link this file without GTK.
 */

#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *omamd_read_file(const char *path, size_t *out_len)
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

char *omamd_read_stdin(size_t *out_len)
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

char *omamd_dup_str(const char *s)
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

char *omamd_dir_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    char *dir;
    size_t n;

    if (!slash)
        return omamd_dup_str(".");
    if (slash == path)
        return omamd_dup_str("/");
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

int omamd_is_markdown_path(const char *path)
{
    return ends_with_ci(path, ".md") ||
           ends_with_ci(path, ".markdown") ||
           ends_with_ci(path, ".mdown") ||
           ends_with_ci(path, ".txt");
}
