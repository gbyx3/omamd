#ifndef OMAMD_UTIL_H
#define OMAMD_UTIL_H

/*
 * util.h — file and path helpers shared by the CLI and the GTK window.
 * No GTK, no GLib.
 */

#include <stddef.h>

#define OMAMD_VERSION "0.2"
#define OMAMD_MAX_FILE_BYTES (32u * 1024u * 1024u)

/* malloc'd bytes; caller free()s.  NULL on error or a file larger
 * than OMAMD_MAX_FILE_BYTES.  out_len may be NULL. */
char *omamd_read_file(const char *path, size_t *out_len);
char *omamd_read_stdin(size_t *out_len);

char *omamd_dup_str(const char *s);
char *omamd_dir_of(const char *path);
int omamd_is_markdown_path(const char *path);

#endif
