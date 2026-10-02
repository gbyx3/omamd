#define _DEFAULT_SOURCE

/*
 * fonts.c — locate the bundled iA Writer Mono S files.
 *
 * iA Writer Mono S is under the SIL Open Font License 1.1
 * (see fonts/OFL.txt).  We look next to the binary, in
 * ~/.local/share/omamd/fonts, and in /usr/share/omamd/fonts.
 */

#include "fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef __linux__
#include <unistd.h>
#endif

static char g_font_dir[4200];

static int is_reg(const char *path)
{
    struct stat st;
    if (!path || !path[0])
        return 0;
    if (stat(path, &st) != 0)
        return 0;
    return S_ISREG(st.st_mode);
}

static int font_dir_ok(const char *dir)
{
    char path[4400];
    if (!dir || !dir[0])
        return 0;
    snprintf(path, sizeof(path), "%s/iAWriterMonoS-Regular.ttf", dir);
    return is_reg(path);
}

static int adopt_font_dir(const char *dir)
{
    char *real;
    if (!font_dir_ok(dir))
        return 0;
    real = realpath(dir, NULL);
    if (real) {
        snprintf(g_font_dir, sizeof(g_font_dir), "%s", real);
        free(real);
    } else {
        snprintf(g_font_dir, sizeof(g_font_dir), "%s", dir);
    }
    return 1;
}

static int exe_dir(char *out, size_t out_sz)
{
    char exe[4096];
    char *slash;
#ifdef __APPLE__
    uint32_t sz = sizeof(exe);
    if (_NSGetExecutablePath(exe, &sz) != 0)
        return 0;
    {
        char *real = realpath(exe, NULL);
        if (real) {
            snprintf(exe, sizeof(exe), "%s", real);
            free(real);
        }
    }
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0)
        return 0;
    exe[n] = '\0';
#else
    (void)exe;
    (void)out;
    (void)out_sz;
    return 0;
#endif
    slash = strrchr(exe, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    snprintf(out, out_sz, "%s", exe);
    return 1;
}

static int argv0_dir(const char *argv0, char *out, size_t out_sz)
{
    char *real;
    char *slash;
    if (!argv0 || !argv0[0])
        return 0;
    real = realpath(argv0, NULL);
    if (!real)
        return 0;
    slash = strrchr(real, '/');
    if (!slash) {
        free(real);
        return 0;
    }
    *slash = '\0';
    snprintf(out, out_sz, "%s", real);
    free(real);
    return 1;
}

int omamd_init_fonts(const char *argv0)
{
    const char *env = getenv("OMAMD_FONTDIR");
    const char *home = getenv("HOME");
    char buf[4200];
    char base[4096];

    g_font_dir[0] = '\0';
    if (env && adopt_font_dir(env))
        return 1;

    if (exe_dir(base, sizeof(base))) {
        snprintf(buf, sizeof(buf), "%s/../fonts", base);
        if (adopt_font_dir(buf))
            return 1;
        snprintf(buf, sizeof(buf), "%s/fonts", base);
        if (adopt_font_dir(buf))
            return 1;
    }
    if (argv0_dir(argv0, base, sizeof(base))) {
        snprintf(buf, sizeof(buf), "%s/../fonts", base);
        if (adopt_font_dir(buf))
            return 1;
        snprintf(buf, sizeof(buf), "%s/fonts", base);
        if (adopt_font_dir(buf))
            return 1;
    }

    if (home) {
        snprintf(buf, sizeof(buf), "%s/.local/share/omamd/fonts", home);
        if (adopt_font_dir(buf))
            return 1;
    }
    if (adopt_font_dir("/usr/share/omamd/fonts"))
        return 1;
    if (adopt_font_dir("fonts"))
        return 1;
    return 0;
}

const char *omamd_font_dir(void)
{
    return g_font_dir;
}
