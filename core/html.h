#ifndef OMAMD_HTML_H
#define OMAMD_HTML_H

/*
 * html.h — palette + HTML fragment → a full document.
 *
 * markdown.c returns a fragment (<h1>…</h1> and so on).  This file
 * wraps that fragment in <!DOCTYPE html>, a <style> block from the
 * palette, and optional @font-face rules for the bundled iA Writer
 * Mono S files.  No GTK, no GLib: the same page is used by --html,
 * the Linux WebKitGTK view, and later WKWebView.
 */

#include "theme.h"

/*
 * CSS for the document (not the GTK window chrome).  Caller free()s.
 * Returns NULL only on out-of-memory.  p NULL uses the default palette.
 */
char *omamd_css(const Palette *p);

/*
 * Full HTML page.  title is escaped.  css may be NULL.  body is the
 * fragment from markdown_to_html() and is inserted as-is.  font_dir
 * is a directory of iAWriterMonoS-*.ttf files, or NULL to skip
 * @font-face.  Caller free()s the result.  NULL only on OOM.
 */
char *omamd_document(const char *title, const char *css, const char *body,
                     const char *font_dir);

#endif
