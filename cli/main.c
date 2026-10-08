/*
 * cli/main.c — omamd without GTK.
 *
 * On a Mac, or any machine without WebKitGTK, this is the `omamd`
 * binary: --html, --term, and a pager when you pass a .md file.
 * The Linux GTK binary has its own main() in linux/gtk.c and links
 * this same cli.c for the flags it shares.
 */

#include "cli.h"
#include "fonts.h"
#include "util.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    OmamdCli o;

    if (omamd_cli_parse(argc, argv, &o) != 0) {
        omamd_cli_usage(stderr);
        return 2;
    }
    if (o.help) {
        omamd_cli_usage(stdout);
        return 0;
    }
    if (o.version) {
        printf("omamd %s\n", OMAMD_VERSION);
        return 0;
    }

    omamd_init_fonts(argv[0]);

    if (o.html)
        return omamd_run_html(o.path, o.theme);
    return omamd_run_term(o.path, o.theme);
}
