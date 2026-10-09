#ifndef MY_LS_FORMAT_H
#define MY_LS_FORMAT_H

#include "entry.h"
#include "options.h"

#include <stdbool.h>
#include <stdio.h>

/* Return an owned display-only copy, or NULL on allocation failure.
 * Raw mode preserves bytes; q mode replaces non-printable/invalid bytes.
 * The caller must free the result. */
char *format_display_name(const char *name, NameMode mode);
/* Entry type takes precedence over executable regular-file status. */
char format_type_suffix(const Entry *);

/* Write one display name per line; return -1 on allocation/output failure. */
int format_listing(FILE *out, const EntryList *, const Options *,
    bool directory_contents);

#endif
