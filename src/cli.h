#ifndef OMAMD_CLI_H
#define OMAMD_CLI_H

/*
 * cli.h — argv, --html, and --term.  Linked by both the GTK binary
 * and the GTK-free `omamd` on machines without WebKitGTK.
 */

#include <stdio.h>

typedef struct {
    int html;
    int term;
    int help;
    int version;
    int err;            /* 2 = bad argv */
    const char *path;   /* optional .md path, or NULL for stdin */
    const char *theme;  /* --theme PATH, or NULL */
} OmamdCli;

int omamd_cli_parse(int argc, char **argv, OmamdCli *o);
void omamd_cli_usage(FILE *out);

int omamd_run_html(const char *path, const char *theme);
int omamd_run_term(const char *path, const char *theme);

/* 1 when WAYLAND_DISPLAY and DISPLAY are both empty (SSH, macOS). */
int omamd_no_display(void);

#endif
