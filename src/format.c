#include "format.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

char *format_display_name(const char *name, NameMode mode)
{
    size_t length = strlen(name);
    size_t offset = 0;
    mbstate_t state = {0};
    char *display;

    if (length == SIZE_MAX) {
        errno = EOVERFLOW;
        return NULL;
    }
    display = malloc(length + 1);
    if (display == NULL)
        return NULL;
    if (mode == NAME_RAW) {
        memcpy(display, name, length + 1);
        return display;
    }
    while (offset < length) {
        wchar_t character;
        size_t consumed = mbrtowc(&character, name + offset,
            length - offset, &state);

        if (consumed == (size_t)-1 || consumed == (size_t)-2 ||
            consumed == 0) {
            /* A decoding error leaves conversion state unusable. Replace
             * one byte, reset, and retry the following byte independently. */
            display[offset++] = '?';
            memset(&state, 0, sizeof(state));
            continue;
        }
        if (iswprint((wint_t)character))
            memcpy(display + offset, name + offset, consumed);
        else
            memset(display + offset, '?', consumed);
        offset += consumed;
    }
    display[length] = '\0';
    return display;
}

char format_type_suffix(const Entry *entry)
{
    mode_t mode = entry->st.st_mode;

    if (S_ISDIR(mode))
        return '/';
    if (S_ISLNK(mode))
        return '@';
#ifdef S_IFWHT
    if ((mode & S_IFMT) == S_IFWHT)
        return '%';
#endif
    if (S_ISSOCK(mode))
        return '=';
    if (S_ISFIFO(mode))
        return '|';
    if (S_ISREG(mode) && (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)
        return '*';
    return '\0';
}

int format_listing(FILE *out, const EntryList *list, const Options *options,
    bool directory_contents)
{
    size_t i;

    /* Later formatting tasks will use this to select directory totals. */
    (void)directory_contents;
    for (i = 0; i < list->length; ++i) {
        char *name = format_display_name(list->items[i].name,
            options->name_mode);
        char suffix = options->classify ?
            format_type_suffix(&list->items[i]) : '\0';
        int result;

        if (name == NULL)
            return -1;
        result = suffix == '\0' ? fprintf(out, "%s\n", name) :
            fprintf(out, "%s%c\n", name, suffix);
        free(name);
        if (result < 0)
            return -1;
    }
    return ferror(out) ? -1 : 0;
}
