#ifndef MY_LS_ENTRY_H
#define MY_LS_ENTRY_H

#include "options.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

typedef struct {
    char *name;
    char *path;
    char *link_target;
    struct stat st;
    unsigned char dirent_type;
    bool metadata_valid;
    bool is_operand;
} Entry;

typedef struct {
    Entry *items;
    size_t length;
    size_t capacity;
} EntryList;

void entry_list_init(EntryList *);
/* Return 0 on success, -1 after a diagnostic on failure. Failed additions
 * preserve existing entries; every successful entry owns its strings. */
int entry_list_add_path(EntryList *, const char *name, const char *path,
    unsigned char dirent_type, bool is_operand, FILE *err);
/* Append in readdir order, retaining successful entries on partial failure. */
int entry_list_read_directory(EntryList *, const char *path,
    const Options *, FILE *err);
bool entry_operand_is_directory(const Entry *, bool no_dereference);
void entry_list_destroy(EntryList *);

#endif
