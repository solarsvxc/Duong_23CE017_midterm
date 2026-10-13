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

/* A list has at most SIZE_MAX entries, each with at most UINTMAX_MAX blocks.
 * Two limbs retain their exact sum even when its final conversion fits only
 * because the selected display unit is large. */
typedef struct { uintmax_t high; uintmax_t low; } BlockSum;

static void add_blocks(BlockSum *sum, blkcnt_t blocks)
{
    uintmax_t previous = sum->low;
    if (blocks <= 0)
        return;
    sum->low += (uintmax_t)blocks;
    if (sum->low < previous)
        ++sum->high;
}

/* Append a numerator bit to binary long division without overflowing 2*r.
 * r < unit; comparing against unit-r gives the next quotient bit safely. */
static int divide_bit(uintmax_t bit, uintmax_t unit, uintmax_t *quotient,
    uintmax_t *remainder)
{
    uintmax_t digit = 0;
    if (*remainder >= unit - *remainder) {
        *remainder -= unit - *remainder;
        digit = 1;
    } else
        *remainder += *remainder;
    if (bit != 0) {
        if (*remainder == unit - 1) {
            *remainder = 0;
            digit = 1;
        } else
            ++*remainder;
    }
    if (*quotient > (UINTMAX_MAX - digit) / 2)
        return -1;
    *quotient = *quotient * 2 + digit;
    return 0;
}

static uintmax_t block_sum_units(BlockSum sum, uint64_t unit_bytes)
{
    uintmax_t unit = unit_bytes == 0 ? 512 : (uintmax_t)unit_bytes;
    uintmax_t quotient = 0, remainder = 0;
    uintmax_t words[] = {sum.high, sum.low};
    size_t word, bit;
    for (word = 0; word < 2; ++word) {
        for (bit = sizeof(uintmax_t) * CHAR_BIT; bit != 0; --bit) {
            if (divide_bit((words[word] >> (bit - 1)) & 1, unit,
                &quotient, &remainder) != 0)
                return UINTMAX_MAX;
        }
    }
    /* Native blocks represent 512 bytes: append nine zero bits instead of
     * multiplying the raw sum, which could require a third limb. */
    for (bit = 0; bit < 9; ++bit) {
        if (divide_bit(0, unit, &quotient, &remainder) != 0)
            return UINTMAX_MAX;
    }
    if (remainder != 0 && quotient != UINTMAX_MAX)
        ++quotient;
    return quotient;
}

uintmax_t format_block_units(blkcnt_t blocks_512, uint64_t unit_bytes)
{
    BlockSum sum = {0};
    add_blocks(&sum, blocks_512);
    return block_sum_units(sum, unit_bytes);
}

int format_human_size(uintmax_t bytes, char *buffer, size_t size)
{
    static const char units[] = {'B', 'K', 'M', 'G', 'T', 'P'};
    uintmax_t scale = 1, whole, remainder, tenths;
    size_t unit = 0;
    int count;
    while (unit < sizeof(units) - 1 && bytes / scale >= 1024) {
        scale *= 1024;
        ++unit;
    }
    whole = bytes / scale;
    remainder = bytes % scale;
    if (unit == 0)
        count = snprintf(buffer, size, "%juB", bytes);
    else if (whole < 10) {
        tenths = whole * 10 + (remainder * 10 + scale / 2) / scale;
        count = snprintf(buffer, size, "%ju.%ju%c", tenths / 10,
            tenths % 10, units[unit]);
    } else {
        if (remainder >= scale / 2)
            ++whole;
        if (whole == 1024 && unit < sizeof(units) - 1)
            count = snprintf(buffer, size, "1.0%c", units[unit + 1]);
        else
            count = snprintf(buffer, size, "%ju%c", whole, units[unit]);
    }
    if (count < 0 || (size_t)count >= size) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

typedef struct {
    char inode[NUMBER_TEXT_SIZE];
    char blocks[NUMBER_TEXT_SIZE];
} PrefixFields;

typedef struct { size_t inode; size_t blocks; } PrefixWidths;

typedef struct {
    PrefixFields prefix;
    char links[NUMBER_TEXT_SIZE];
    char size[2 * NUMBER_TEXT_SIZE + 2];
    char timestamp[128];
    char *owner;
    char *group;
} LongFields;

typedef struct {
    PrefixWidths prefix;
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
            (uintmax_t)(unsigned int)major(entry->st.st_rdev),
            (uintmax_t)(unsigned int)minor(entry->st.st_rdev));
    else if (options->size_mode == SIZE_HUMAN) {
        if (format_human_size(entry->st.st_size <= 0 ? 0 :
            (uintmax_t)entry->st.st_size, fields->size,
            sizeof(fields->size)) != 0)
            return -1;
        count = (int)strlen(fields->size);
    } else
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

static int block_text(BlockSum sum, const Options *options,
    char *buffer, size_t size)
{
    uint64_t unit = options->size_mode == SIZE_KIB ? 1024 : options->block_size;
    int count;
    if (options->size_mode == SIZE_HUMAN)
        return format_human_size(block_sum_units(sum, 1), buffer, size);
    count = snprintf(buffer, size, "%ju", block_sum_units(sum, unit));
    if (count < 0 || (size_t)count >= size) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

static int prefix_fields(const Entry *entry, const Options *options,
    PrefixFields *fields)
{
    int count;
    if (options->show_inode) {
        count = snprintf(fields->inode, sizeof(fields->inode), "%ju",
            (uintmax_t)entry->st.st_ino);
        if (count < 0 || (size_t)count >= sizeof(fields->inode)) {
            errno = EOVERFLOW;
            return -1;
        }
    }
    if (options->show_blocks) {
        BlockSum sum = {0};
        add_blocks(&sum, entry->st.st_blocks);
        if (block_text(sum, options, fields->blocks,
            sizeof(fields->blocks)) != 0)
            return -1;
    }
    return 0;
}

static void measure_prefix(PrefixWidths *widths, const PrefixFields *fields,
    const Options *options)
{
    if (options->show_inode)
        measure_width(&widths->inode, fields->inode);
    if (options->show_blocks)
        measure_width(&widths->blocks, fields->blocks);
}

static int print_prefix(FILE *out, const PrefixFields *fields,
    const PrefixWidths *widths, const Options *options)
{
    if (options->show_inode &&
        column(out, fields->inode, widths->inode, true) != 0)
        return -1;
    if (options->show_blocks &&
        column(out, fields->blocks, widths->blocks, true) != 0)
        return -1;
    return 0;
}

static int long_listing(FILE *out, const EntryList *list,
    const Options *options, const char **error_path, FormatProgress *progress)
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
        if (progress != NULL)
            progress->failed_index = i;
        if (error_path != NULL)
            *error_path = list->items[i].path;
        if (prefix_fields(&list->items[i], options, &fields[i].prefix) != 0 ||
            long_fields(&list->items[i], options, &fields[i]) != 0)
            goto cleanup;
        measure_prefix(&widths.prefix, &fields[i].prefix, options);
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
        char *name;
        int written;

        if (progress != NULL)
            progress->failed_index = i;
        if (error_path != NULL)
            *error_path = entry->path;
        name = format_display_name(entry->name, options->name_mode);
        if (name == NULL)
            goto cleanup;
        format_mode(entry->st.st_mode, entry->dirent_type, mode);
        written = print_prefix(out, &fields[i].prefix, &widths.prefix, options);
        if (written == 0)
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
        error = errno;
        free(name);
        errno = error;
        if (written < 0)
            goto cleanup;
        if (suffix != '\0' && fputc(suffix, out) == EOF)
            goto cleanup;
        if (S_ISLNK(entry->st.st_mode) && entry->link_target != NULL &&
            fprintf(out, " -> %s", entry->link_target) < 0)
            goto cleanup;
        if (fputc('\n', out) == EOF)
            goto cleanup;
        if (progress != NULL)
            ++progress->completed_entries;
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
    bool directory_contents, const char **error_path, FormatProgress *progress)
{
    size_t i;
    PrefixWidths widths = {0};

    if (error_path != NULL)
        *error_path = NULL;
    if (progress != NULL)
        *progress = (FormatProgress){SIZE_MAX, 0};
    if (directory_contents && (options->long_format ||
        (options->show_blocks && options->stdout_is_tty))) {
        BlockSum sum = {0};
        char total[NUMBER_TEXT_SIZE];
        for (i = 0; i < list->length; ++i)
            add_blocks(&sum, list->items[i].st.st_blocks);
        if (block_text(sum, options, total, sizeof(total)) != 0 ||
            fprintf(out, "total %s\n", total) < 0)
            return -1;
    }
    if (options->long_format)
        return long_listing(out, list, options, error_path, progress);
    for (i = 0; i < list->length; ++i) {
        PrefixFields fields;
        if (progress != NULL)
            progress->failed_index = i;
        if (error_path != NULL)
            *error_path = list->items[i].path;
        if (prefix_fields(&list->items[i], options, &fields) != 0)
            return -1;
        measure_prefix(&widths, &fields, options);
    }
    for (i = 0; i < list->length; ++i) {
        PrefixFields fields;
        char *name;
        char suffix = options->classify ?
            format_type_suffix(&list->items[i]) : '\0';
        int result, error;

        if (progress != NULL)
            progress->failed_index = i;
        if (error_path != NULL)
            *error_path = list->items[i].path;
        name = format_display_name(list->items[i].name, options->name_mode);
        if (name == NULL)
            return -1;
        result = prefix_fields(&list->items[i], options, &fields);
        if (result == 0)
            result = print_prefix(out, &fields, &widths, options);
        if (result == 0)
            result = suffix == '\0' ? fprintf(out, "%s\n", name) :
                fprintf(out, "%s%c\n", name, suffix);
        error = errno;
        free(name);
        errno = error;
        if (result < 0)
            return -1;
        if (progress != NULL)
            ++progress->completed_entries;
    }
    return ferror(out) ? -1 : 0;
}
