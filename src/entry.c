#include "entry.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int entry_error(FILE *err, const char *path, int error)
{
    fprintf(err, "my_ls: %s: %s\n", path, strerror(error));
    errno = error;
    return -1;
}

static char *copy_string(const char *input)
{
    size_t length = strlen(input);
    char *result;
    if (length == SIZE_MAX) {
        errno = EOVERFLOW;
        return NULL;
    }
    result = malloc(length + 1);
    if (result != NULL)
        memcpy(result, input, length + 1);
    return result;
}

static char *join_path(const char *parent, const char *name)
{
    size_t parent_length = strlen(parent);
    size_t name_length = strlen(name);
    bool slash = parent_length != 0 && parent[parent_length - 1] != '/';
    size_t extra = slash ? 2 : 1;
    size_t offset = parent_length;
    char *path;
    if (parent_length > SIZE_MAX - name_length ||
        parent_length + name_length > SIZE_MAX - extra) {
        errno = EOVERFLOW;
        return NULL;
    }
    path = malloc(parent_length + name_length + extra);
    if (path == NULL)
        return NULL;
    memcpy(path, parent, parent_length);
    if (slash)
        path[offset++] = '/';
    memcpy(path + offset, name, name_length + 1);
    return path;
}

static char *read_link_target(const char *path)
{
    size_t capacity = 128;
    char *target = malloc(capacity + 1);
    if (target == NULL)
        return NULL;
    for (;;) {
        ssize_t length = readlink(path, target, capacity);
        char *grown;
        int error;
        if (length < 0) {
            error = errno;
            free(target);
            errno = error;
            return NULL;
        }
        if ((size_t)length < capacity) {
            target[(size_t)length] = '\0';
            return target;
        }
        if (capacity > (size_t)SSIZE_MAX / 2 || capacity > (SIZE_MAX - 1) / 2) {
            free(target);
            errno = EOVERFLOW;
            return NULL;
        }
        capacity *= 2;
        grown = realloc(target, capacity + 1);
        if (grown == NULL) {
            error = errno;
            free(target);
            errno = error;
            return NULL;
        }
        target = grown;
    }
}

static void entry_destroy(Entry *entry)
{
    free(entry->name);
    free(entry->path);
    free(entry->link_target);
}

static int entry_list_grow(EntryList *list)
{
    const size_t maximum = SIZE_MAX / sizeof(*list->items);
    size_t capacity;
    Entry *items;
    if (list->length < list->capacity)
        return 0;
    if (list->length >= maximum) {
        errno = EOVERFLOW;
        return -1;
    }
    if (list->capacity == 0)
        capacity = maximum < 16 ? maximum : 16;
    else
        capacity = list->capacity > maximum / 2 ? maximum : list->capacity * 2;
    items = realloc(list->items, capacity * sizeof(*list->items));
    if (items == NULL)
        return -1;
    list->items = items;
    list->capacity = capacity;
    return 0;
}

void entry_list_init(EntryList *list)
{
    *list = (EntryList){0};
}

int entry_list_add_path(EntryList *list, const char *name, const char *path,
    unsigned char dirent_type, bool is_operand, FILE *err)
{
    Entry entry = {0};
    int error;
    entry.dirent_type = dirent_type;
    entry.is_operand = is_operand;
    entry.name = copy_string(name);
    if (entry.name == NULL)
        goto failure;
    entry.path = copy_string(path);
    if (entry.path == NULL)
        goto failure;
    if (lstat(path, &entry.st) == 0) {
        entry.metadata_valid = true;
    } else {
#if defined(DT_WHT) && defined(S_IFWHT)
        if (dirent_type != DT_WHT)
            goto failure;
        entry.st = (struct stat){0};
        entry.st.st_mode = S_IFWHT;
#else
        goto failure;
#endif
    }
    if (entry.metadata_valid && S_ISLNK(entry.st.st_mode)) {
        entry.link_target = read_link_target(path);
        if (entry.link_target == NULL)
            goto failure;
    }
    if (entry_list_grow(list) != 0)
        goto failure;
    list->items[list->length++] = entry;
    return 0;

failure:
    error = errno;
    entry_destroy(&entry);
    return entry_error(err, path, error);
}

static bool include_name(const char *name, const Options *options)
{
    if (name[0] != '.' || options->show_all)
        return true;
    if (!options->almost_all)
        return false;
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

int entry_list_read_directory(EntryList *list, const char *path,
    const Options *options, FILE *err)
{
    DIR *directory = opendir(path);
    int first_error = 0;
    if (directory == NULL)
        return entry_error(err, path, errno);
    for (;;) {
        struct dirent *item;
        char *child_path;
        int result, error;
        errno = 0;
        item = readdir(directory);
        if (item == NULL) {
            if (errno != 0) {
                error = errno;
                entry_error(err, path, error);
                if (first_error == 0)
                    first_error = error;
            }
            break;
        }
        if (!include_name(item->d_name, options))
            continue;
        child_path = join_path(path, item->d_name);
        if (child_path == NULL) {
            error = errno;
            entry_error(err, path, error);
            if (first_error == 0)
                first_error = error;
            break;
        }
        result = entry_list_add_path(list, item->d_name, child_path,
            item->d_type, false, err);
        error = errno;
        free(child_path);
        if (result != 0) {
            if (first_error == 0)
                first_error = error;
            if (error == ENOMEM || error == EOVERFLOW)
                break;
        }
    }
    if (closedir(directory) != 0) {
        int error = errno;
        entry_error(err, path, error);
        if (first_error == 0)
            first_error = error;
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

bool entry_operand_is_directory(const Entry *entry, bool no_dereference)
{
    struct stat target;
    if (!entry->metadata_valid)
        return false;
    if (S_ISDIR(entry->st.st_mode))
        return true;
    if (!entry->is_operand || no_dereference || !S_ISLNK(entry->st.st_mode))
        return false;
    return stat(entry->path, &target) == 0 && S_ISDIR(target.st_mode);
}

void entry_list_destroy(EntryList *list)
{
    size_t i;
    for (i = 0; i < list->length; ++i)
        entry_destroy(&list->items[i]);
    free(list->items);
    entry_list_init(list);
}
