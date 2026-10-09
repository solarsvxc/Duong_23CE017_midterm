#ifndef MY_LS_SORT_H
#define MY_LS_SORT_H

#include "entry.h"
#include "options.h"

/* Reorders complete entries in place; SORT_NONE preserves their input order.
 * Uses one active qsort context and is not reentrant or thread-safe. */
void entry_list_sort(EntryList *, const Options *);

#endif
