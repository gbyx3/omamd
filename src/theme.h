#ifndef OMAMD_THEME_H
#define OMAMD_THEME_H

/*
 * theme.h — palette loading, independent of GTK and of Omarchy.
 *
 * A palette is a handful of hex colours used by the HTML preview,
 * the GTK chrome, and the terminal renderer.  The file format is
 * the same colors.toml Omarchy themes ship.
 *
 * Where the file comes from, in order:
 *
 *   1. --theme PATH          (or a directory that contains colors.toml)
 *   2. $OMAMD_THEME          (same)
 *   3. Omarchy live theme    ~/.local/state/omarchy/current/theme/colors.toml
 *   4. User copy-paste file  $XDG_CONFIG_HOME/omamd/colors.toml
 *                            else ~/.config/omamd/colors.toml
 *
 * If none of those exist, palette_load() fills in a dark default.
 */

#include <stddef.h>

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

typedef enum {
    THEME_KIND_NONE = 0,
    THEME_KIND_EXPLICIT, /* --theme or OMAMD_THEME */
    THEME_KIND_OMARCHY,  /* live Omarchy current/theme */
    THEME_KIND_CONFIG    /* ~/.config/omamd/colors.toml */
} ThemeKind;

void palette_default(Palette *p);

/* Read PATH into p.  Missing keys keep whatever p already has, so
 * call palette_default first.  Returns 1 if the file was read. */
int palette_load_file(Palette *p, const char *path);

/* Default palette, then the first file theme_resolve() finds. */
void palette_load(Palette *p, const char *cli_theme);

/* Put the chosen colors.toml path in out.  warn: print a one-shot
 * message if --theme / OMAMD_THEME was set but the file is missing. */
ThemeKind theme_resolve(const char *cli_theme, char *out, size_t out_sz,
                        int warn);

void theme_omarchy_current_path(char *out, size_t out_sz, const char *leaf);
void theme_user_config_path(char *out, size_t out_sz);

/* 1 if ~/.local/state/omarchy/current is a directory (theme switch
 * in progress still counts).  out gets the path without a trailing
 * slash, or "" if HOME is unset. */
int theme_omarchy_live_dir(char *out, size_t out_sz);

int theme_parent_dir(const char *path, char *out, size_t out_sz);
int theme_path_is_file(const char *path);
int theme_path_is_dir(const char *path);

#endif
