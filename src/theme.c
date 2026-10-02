/*
 * theme.c — find a colors.toml and turn it into a Palette.
 *
 * This file does not include GTK.  The same loader works for --html,
 * --term, and the window, and it works on a machine that has never
 * heard of Omarchy: paste a colors.toml at ~/.config/omamd/colors.toml
 * or pass --theme PATH.
 */

#include "theme.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define THEME_MAX_BYTES (256u * 1024u)

int theme_path_is_file(const char *path)
{
    struct stat st;
    if (!path || !path[0])
        return 0;
    if (stat(path, &st) != 0)
        return 0;
    return S_ISREG(st.st_mode);
}

int theme_path_is_dir(const char *path)
{
    struct stat st;
    if (!path || !path[0])
        return 0;
    if (stat(path, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode);
}

int theme_parent_dir(const char *path, char *out, size_t out_sz)
{
    const char *slash;
    size_t n;

    if (!out || out_sz == 0)
        return 0;
    out[0] = '\0';
    if (!path || !path[0])
        return 0;
    slash = strrchr(path, '/');
    if (!slash)
        return 0;
    if (slash == path) {
        snprintf(out, out_sz, "/");
        return 1;
    }
    n = (size_t)(slash - path);
    if (n >= out_sz)
        n = out_sz - 1;
    memcpy(out, path, n);
    out[n] = '\0';
    return 1;
}

void theme_omarchy_current_path(char *out, size_t out_sz, const char *leaf)
{
    const char *home = getenv("HOME");

    if (!out || out_sz == 0)
        return;
    if (!home || !leaf) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_sz, "%s/.local/state/omarchy/current/%s", home, leaf);
}

int theme_omarchy_live_dir(char *out, size_t out_sz)
{
    size_t n;

    theme_omarchy_current_path(out, out_sz, "");
    if (!out || !out[0])
        return 0;
    n = strlen(out);
    if (n > 0 && out[n - 1] == '/')
        out[n - 1] = '\0';
    return theme_path_is_dir(out);
}

void theme_user_config_path(char *out, size_t out_sz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");

    if (!out || out_sz == 0)
        return;
    if (xdg && xdg[0]) {
        snprintf(out, out_sz, "%s/omamd/colors.toml", xdg);
        return;
    }
    if (home && home[0]) {
        snprintf(out, out_sz, "%s/.config/omamd/colors.toml", home);
        return;
    }
    out[0] = '\0';
}

static void join_colors_toml(const char *dir, char *out, size_t out_sz)
{
    size_t n = strlen(dir);
    if (n > 0 && dir[n - 1] == '/')
        snprintf(out, out_sz, "%scolors.toml", dir);
    else
        snprintf(out, out_sz, "%s/colors.toml", dir);
}

/* If candidate is a directory, look for colors.toml inside it.
 * Otherwise use the path as-is.  Returns 1 if the result is a file. */
static int expand_theme_candidate(const char *candidate, char *out, size_t out_sz)
{
    if (!candidate || !candidate[0] || !out || out_sz == 0) {
        if (out && out_sz)
            out[0] = '\0';
        return 0;
    }
    if (theme_path_is_dir(candidate))
        join_colors_toml(candidate, out, out_sz);
    else
        snprintf(out, out_sz, "%s", candidate);
    return theme_path_is_file(out);
}

static char *read_theme_file(const char *path)
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
    if (size < 0 || (size_t)size > THEME_MAX_BYTES) {
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
    return buf;
}

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

void palette_default(Palette *p)
{
    /* A warm-dark fallback when no colors.toml is around. */
    snprintf(p->bg, sizeof(p->bg), "%s", "#1e1e2e");
    snprintf(p->fg, sizeof(p->fg), "%s", "#cdd6f4");
    snprintf(p->muted, sizeof(p->muted), "%s", "#6c7086");
    snprintf(p->accent, sizeof(p->accent), "%s", "#89b4fa");
    snprintf(p->code_bg, sizeof(p->code_bg), "%s", "#11111b");
    snprintf(p->surface, sizeof(p->surface), "%s", "#313244");
    snprintf(p->sel, sizeof(p->sel), "%s", "#45475a");
    p->dark = 1;
}

int palette_load_file(Palette *p, const char *path)
{
    char *toml;
    char buf[64];

    if (!p || !path || !path[0])
        return 0;
    toml = read_theme_file(path);
    if (!toml)
        return 0;

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
    return 1;
}

ThemeKind theme_resolve(const char *cli_theme, char *out, size_t out_sz,
                        int warn)
{
    static int warned_cli;
    static int warned_env;
    const char *env;
    char tmp[512];

    if (out && out_sz)
        out[0] = '\0';
    if (!out || out_sz == 0)
        return THEME_KIND_NONE;

    if (cli_theme && cli_theme[0]) {
        if (expand_theme_candidate(cli_theme, out, out_sz))
            return THEME_KIND_EXPLICIT;
        if (warn && !warned_cli) {
            warned_cli = 1;
            fprintf(stderr, "omamd: theme file not found: %s\n", cli_theme);
        }
        out[0] = '\0';
    }

    env = getenv("OMAMD_THEME");
    if (env && env[0]) {
        if (expand_theme_candidate(env, out, out_sz))
            return THEME_KIND_EXPLICIT;
        if (warn && !warned_env) {
            warned_env = 1;
            fprintf(stderr, "omamd: OMAMD_THEME not found: %s\n", env);
        }
        out[0] = '\0';
    }

    theme_omarchy_current_path(tmp, sizeof(tmp), "theme/colors.toml");
    if (theme_path_is_file(tmp)) {
        snprintf(out, out_sz, "%s", tmp);
        return THEME_KIND_OMARCHY;
    }

    theme_user_config_path(tmp, sizeof(tmp));
    if (theme_path_is_file(tmp)) {
        snprintf(out, out_sz, "%s", tmp);
        return THEME_KIND_CONFIG;
    }

    return THEME_KIND_NONE;
}

void palette_load(Palette *p, const char *cli_theme)
{
    char path[512];

    palette_default(p);
    if (theme_resolve(cli_theme, path, sizeof(path), 1) != THEME_KIND_NONE)
        palette_load_file(p, path);
}
