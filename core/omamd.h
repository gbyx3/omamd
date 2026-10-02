#ifndef OMAMD_H
#define OMAMD_H

/*
 * omamd.h — the C ABI the Apple shells and the CLI share.
 *
 * Swift's bridging header includes this file.  markdown_to_html(),
 * palette_load() / palette_load_file(), and omamd_document() are
 * the product.  Caller free()s the char * results.
 */

#include "markdown.h"
#include "theme.h"
#include "html.h"
#include "util.h"

#endif
