#ifndef OMAMD_FONTS_H
#define OMAMD_FONTS_H

/*
 * fonts.h — find the bundled iA Writer Mono S directory.
 *
 * Looks at $OMAMD_FONTDIR, next to the executable, the user/share
 * install paths, and ./fonts.  No fontconfig: the GTK window
 * registers the files itself after this returns.
 */

/* Fill the cached directory.  argv0 may be NULL.  Returns 1 if a
 * directory with iAWriterMonoS-Regular.ttf was found. */
int omamd_init_fonts(const char *argv0);

/* "" if omamd_init_fonts() found nothing. */
const char *omamd_font_dir(void);

#endif
