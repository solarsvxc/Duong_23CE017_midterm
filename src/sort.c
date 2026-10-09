#include "sort.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* qsort has no context argument. The caller establishes this only for the
 * synchronous sort, then clears it; callers must serialize sorting calls. */
static const Options *active_options;

static struct timespec entry_time(const Entry *entry)
{
    switch (active_options->time_mode) {
    case TIME_CTIME:
        return entry->st.st_ctim;
    case TIME_ATIME:
        return entry->st.st_atim;
    case TIME_MTIME:
    default:
        return entry->st.st_mtim;
    }
}

static int compare_entries(const void *left, const void *right)
{
    const Entry *a = left;
    const Entry *b = right;
    int order = 0;

    if (active_options->sort_mode == SORT_SIZE) {
        if (a->st.st_size > b->st.st_size)
            order = -1;
        else if (a->st.st_size < b->st.st_size)
            order = 1;
    } else if (active_options->sort_mode == SORT_TIME) {
        struct timespec a_time = entry_time(a);
        struct timespec b_time = entry_time(b);
        if (a_time.tv_sec > b_time.tv_sec)
            order = -1;
        else if (a_time.tv_sec < b_time.tv_sec)
            order = 1;
        else if (a_time.tv_nsec > b_time.tv_nsec)
            order = -1;
        else if (a_time.tv_nsec < b_time.tv_nsec)
            order = 1;
    }

    if (order == 0) {
        int name_order = strcmp(a->name, b->name);
        /* Normalize strcmp before negation: its result need not be +/-1. */
        if (name_order < 0)
            order = -1;
        else if (name_order > 0)
            order = 1;
    }
    return active_options->reverse ? -order : order;
}

void entry_list_sort(EntryList *list, const Options *options)
{
    if (options->sort_mode == SORT_NONE || list->length < 2)
        return;
    active_options = options;
    qsort(list->items, list->length, sizeof(list->items[0]), compare_entries);
    active_options = NULL;
}
