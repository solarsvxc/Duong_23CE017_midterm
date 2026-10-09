#include "format.h"

int format_listing(FILE *out, const EntryList *list, const Options *options,
    bool directory_contents)
{
    size_t i;

    /* Later formatting tasks will use these to select fields and totals. */
    (void)options;
    (void)directory_contents;
    for (i = 0; i < list->length; ++i) {
        if (fprintf(out, "%s\n", list->items[i].name) < 0)
            return -1;
    }
    return ferror(out) ? -1 : 0;
}
