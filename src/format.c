#include "format.h"

#include <errno.h>
#include <dirent.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

void format_mode(mode_t mode, unsigned char dirent_type, char out[11])
{
    static const mode_t permissions[] = {
        S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP,
        S_IROTH, S_IWOTH, S_IXOTH
    };
    static const char symbols[] = "rwxrwxrwx";
    size_t i;

    out[0] = '?';
    if (S_ISREG(mode))
        out[0] = '-';
    else if (S_ISDIR(mode))
        out[0] = 'd';
    else if (S_ISLNK(mode))
        out[0] = 'l';
    else if (S_ISBLK(mode))
        out[0] = 'b';
    else if (S_ISCHR(mode))
        out[0] = 'c';
    else if (S_ISSOCK(mode))
        out[0] = 's';
    else if (S_ISFIFO(mode))
        out[0] = 'p';
#ifdef S_IFWHT
    else if ((mode & S_IFMT) == S_IFWHT)
        out[0] = 'w';
#endif
#ifdef DT_WHT
    if (out[0] == '?' && dirent_type == DT_WHT)
        out[0] = 'w';
#else
    (void)dirent_type;
#endif
    for (i = 0; i < sizeof(permissions) / sizeof(permissions[0]); ++i)
        out[i + 1] = (mode & permissions[i]) != 0 ? symbols[i] : '-';
    if ((mode & S_ISUID) != 0)
        out[3] = (mode & S_IXUSR) != 0 ? 's' : 'S';
    if ((mode & S_ISGID) != 0)
        out[6] = (mode & S_IXGRP) != 0 ? 's' : 'S';
    if ((mode & S_ISVTX) != 0)
        out[9] = (mode & S_IXOTH) != 0 ? 't' : 'T';
    out[10] = '\0';
}

/* A binary digit bound also bounds decimal text, plus sign and NUL. */
#define NUMBER_TEXT_SIZE (sizeof(uintmax_t) * CHAR_BIT + 2)

typedef struct {
    char links[NUMBER_TEXT_SIZE];
    char size[2 * NUMBER_TEXT_SIZE + 2];
    char timestamp[128];
    char *owner;
    char *group;
} LongFields;

typedef struct {
    size_t links;
    size_t owner;
    size_t group;
    size_t size;
    size_t timestamp;
} LongWidths;

static char *account_text(const char *name, uintmax_t id)
{
    char number[NUMBER_TEXT_SIZE];
    if (name == NULL) {
        int count = snprintf(number, sizeof(number), "%ju", id);
        if (count < 0 || (size_t)count >= sizeof(number)) {
            errno = EOVERFLOW;
            return NULL;
        }
        name = number;
    }
    return format_display_name(name, NAME_RAW);
}

static time_t selected_time(const Entry *entry, TimeMode mode)
{
    switch (mode) {
    case TIME_CTIME: return entry->st.st_ctim.tv_sec;
    case TIME_ATIME: return entry->st.st_atim.tv_sec;
    case TIME_MTIME:
    default: return entry->st.st_mtim.tv_sec;
    }
}

static int long_fields(const Entry *entry, const Options *options,
    LongFields *fields)
{
    struct passwd *owner = NULL;
    struct group *group = NULL;
    struct tm *calendar;
    time_t timestamp = selected_time(entry, options->time_mode);
    int count;

    if (!options->numeric_ids)
        owner = getpwuid(entry->st.st_uid);
    /* Copy immediately: account database functions may reuse static storage. */
    fields->owner = account_text(owner == NULL ? NULL : owner->pw_name,
        (uintmax_t)entry->st.st_uid);
    if (fields->owner == NULL)
        return -1;
    if (!options->numeric_ids)
        group = getgrgid(entry->st.st_gid);
    fields->group = account_text(group == NULL ? NULL : group->gr_name,
        (uintmax_t)entry->st.st_gid);
    if (fields->group == NULL)
        return -1;
    count = snprintf(fields->links, sizeof(fields->links), "%ju",
        (uintmax_t)entry->st.st_nlink);
    if (count < 0 || (size_t)count >= sizeof(fields->links)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (S_ISBLK(entry->st.st_mode) || S_ISCHR(entry->st.st_mode))
        count = snprintf(fields->size, sizeof(fields->size), "%ju, %ju",
            (uintmax_t)major(entry->st.st_rdev),
            (uintmax_t)minor(entry->st.st_rdev));
    else
        count = snprintf(fields->size, sizeof(fields->size), "%jd",
            (intmax_t)entry->st.st_size);
    if (count < 0 || (size_t)count >= sizeof(fields->size)) {
        errno = EOVERFLOW;
        return -1;
    }
    calendar = localtime(&timestamp);
    if (calendar == NULL)
        return -1;
    if (strftime(fields->timestamp, sizeof(fields->timestamp),
        "%b %e %H:%M", calendar) == 0) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

static void measure_width(size_t *width, const char *text)
{
    size_t length = strlen(text);
    if (length > *width)
        *width = length;
}

static int padding(FILE *out, size_t count)
{
    while (count != 0) {
        if (fputc(' ', out) == EOF)
            return -1;
        --count;
    }
    return 0;
}

/* size_t padding avoids narrowing large account-name widths to printf int. */
static int column(FILE *out, const char *text, size_t width, bool right)
{
    size_t spaces = width - strlen(text);
    if (right && padding(out, spaces) != 0)
        return -1;
    if (fputs(text, out) == EOF)
        return -1;
    if (!right && padding(out, spaces) != 0)
        return -1;
    return fputc(' ', out) == EOF ? -1 : 0;
}

static int long_listing(FILE *out, const EntryList *list,
    const Options *options)
{
    LongFields *fields;
    LongWidths widths = {0};
    size_t i;
    int result = -1;
    int error;

    if (list->length == 0)
        return ferror(out) ? -1 : 0;
    if (list->length > SIZE_MAX / sizeof(*fields)) {
        errno = EOVERFLOW;
        return -1;
    }
    fields = calloc(list->length, sizeof(*fields));
    if (fields == NULL)
        return -1;
    for (i = 0; i < list->length; ++i) {
        if (long_fields(&list->items[i], options, &fields[i]) != 0)
            goto cleanup;
        measure_width(&widths.links, fields[i].links);
        measure_width(&widths.owner, fields[i].owner);
        measure_width(&widths.group, fields[i].group);
        measure_width(&widths.size, fields[i].size);
        measure_width(&widths.timestamp, fields[i].timestamp);
    }
    for (i = 0; i < list->length; ++i) {
        const Entry *entry = &list->items[i];
        char mode[11];
        char suffix = options->classify ? format_type_suffix(entry) : '\0';
        char *name = format_display_name(entry->name, options->name_mode);
        int written;

        if (name == NULL)
            goto cleanup;
        format_mode(entry->st.st_mode, entry->dirent_type, mode);
        written = fprintf(out, "%s ", mode);
        if (written >= 0 &&
            (column(out, fields[i].links, widths.links, true) != 0 ||
            column(out, fields[i].owner, widths.owner, false) != 0 ||
            column(out, fields[i].group, widths.group, false) != 0 ||
            column(out, fields[i].size, widths.size, true) != 0 ||
            column(out, fields[i].timestamp, widths.timestamp, false) != 0))
            written = -1;
        if (written >= 0)
            written = fputs(name, out);
        free(name);
        if (written < 0)
            goto cleanup;
        if (suffix != '\0' && fputc(suffix, out) == EOF)
            goto cleanup;
        if (S_ISLNK(entry->st.st_mode) && entry->link_target != NULL &&
            fprintf(out, " -> %s", entry->link_target) < 0)
            goto cleanup;
        if (fputc('\n', out) == EOF)
            goto cleanup;
    }
    result = ferror(out) ? -1 : 0;

cleanup:
    error = errno;
    for (i = 0; i < list->length; ++i) {
        free(fields[i].owner);
        free(fields[i].group);
    }
    free(fields);
    errno = error;
    return result;
}

int format_listing(FILE *out, const EntryList *list, const Options *options,
    bool directory_contents)
{
    size_t i;

    /* Later formatting tasks will use this to select directory totals. */
    (void)directory_contents;
    if (options->long_format)
        return long_listing(out, list, options);
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
