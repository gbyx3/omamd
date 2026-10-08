#ifndef OMAMD_MARKDOWN_H
#define OMAMD_MARKDOWN_H

/*
 * markdown.h — the public "menu" of our Markdown converter.
 *
 * A .h file is called a *header*.  It is not a program by itself.
 * Any .c file that writes `#include "markdown.h"` gets a copy of
 * whatever is in here.  That is how C shares function declarations
 * across files.
 *
 * The #ifndef / #define / #endif trio is a "header guard".  It stops
 * the file from being pasted in twice, which would make the compiler
 * complain about duplicate declarations.
 *
 * -----------------------------------------------------------------
 * A 30-second C primer for this project
 * -----------------------------------------------------------------
 *
 *   int n = 3;           n is a box holding the integer 3
 *   char *s;             s is a box holding an ADDRESS of a character
 *                        (usually the first character of some text)
 *   const char *s;       "I will read the text, never write through s"
 *   size_t n;            an integer meant for counting bytes or items
 *   struct { ... } Buf;  a bundle of fields sitting next to each other
 *
 * C has no built-in string type and no garbage collector.  Text is
 * just bytes in a row, ending with a 0 byte ('\0') if it is a
 * "C string".  You ask the OS for memory with malloc() and give it
 * back with free().  Forgetting free() is a "memory leak".
 *
 * This function returns HTML the caller owns: you must free() it.
 */

#include <stddef.h>  /* for size_t */

/*
 * Turn Markdown bytes into a freshly allocated HTML fragment.
 *
 *   md  — pointer to the first byte of the Markdown text
 *   n   — how many bytes long that text is (it does not have to
 *         end with '\0', so this works on file buffers)
 *
 * Returns a '\0'-terminated HTML string on success.  The caller
 * must pass that pointer to free() when done.
 *
 * Returns NULL only if the computer ran out of memory.
 *
 * What we understand (a useful everyday subset, not every corner
 * of the CommonMark spec):
 *
 *   # headings          **bold** *italic* ~~strike~~ `code`
 *   fenced code blocks  indented code
 *   lists (- * + 1.)    task lists (- [ ] / - [x])
 *   > block quotes      --- horizontal rules
 *   [links](url)        ![images](url)
 *   tables              autolinks  http://...  and <http://...>
 *   Setext headings     (underlines of === or ---)
 */
char *markdown_to_html(const char *md, size_t n);

#endif /* OMAMD_MARKDOWN_H */
