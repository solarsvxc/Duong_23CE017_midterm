#ifndef MY_LS_FORMAT_H
#define MY_LS_FORMAT_H

#include "entry.h"
#include "options.h"

#include <stdbool.h>
#include <stdio.h>

/* Write one plain name per line; return -1 if the output stream fails. */
int format_listing(FILE *out, const EntryList *, const Options *,
    bool directory_contents);

#endif
