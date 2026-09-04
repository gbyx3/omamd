#ifndef OMAMD_TERM_H
#define OMAMD_TERM_H

#include <stddef.h>

/*
 * Terminal renderer: Markdown → ANSI (or plain text), then either
 * dump to stdout or run a small pager.  No GTK.  Safe over SSH.
 */

typedef struct {
    int color;           /* 0 = no escapes */
    unsigned fg;         /* 0xRRGGBB */
    unsigned bg;
    unsigned accent;
    unsigned muted;
    unsigned code_bg;
} TermPalette;

char *markdown_to_term(const char *md, size_t n, const TermPalette *pal);

/*
 * Show `md`.  If stdout is a TTY, open a pager (q to quit, f to
 * toggle follow).  If watch_path is set, reread that file when it
 * changes; follow (on by default) jumps to the bottom.  reread()
 * must return a malloc'd buffer or NULL.
 */
int term_run(const char *watch_path, const char *md, size_t n,
             const TermPalette *pal,
             char *(*reread)(const char *path, size_t *n));

#endif
