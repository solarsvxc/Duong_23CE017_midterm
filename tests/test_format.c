#include "format.h"

#include <assert.h>
#include <dirent.h>
#include <grp.h>
#include <inttypes.h>
#include <locale.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Aliasing input, changing raw bytes, or failing to advance invalid input
 * breaks these literal expectations. */
static void display_name_bytes(void)
{
    static const struct {
        const char *input;
        const char *expected;
    } cases[] = {
        {"", ""}, {"plain name?", "plain name?"},
        {"a\001b\t\n\177", "a?b???"},
        {"a\377b", "a?b"}, {"\303\251", "??"},
        {"\342\202", "??"}, {"\377\376z", "??z"}
    };
    size_t i;
    assert(setlocale(LC_CTYPE, "C") != NULL);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char *raw = format_display_name(cases[i].input, NAME_RAW);
        char *safe = format_display_name(cases[i].input, NAME_QMARK);
        assert(raw != NULL && safe != NULL);
        assert(raw != cases[i].input && safe != cases[i].input);
        assert(memcmp(raw, cases[i].input, strlen(cases[i].input) + 1) == 0);
        assert(strcmp(safe, cases[i].expected) == 0);
        free(raw);
        free(safe);
    }
    {
        char input[] = "name";
        char *copy = format_display_name(input, NAME_RAW);
        assert(copy != NULL);
        copy[0] = 'N';
        assert(strcmp(input, "name") == 0);
        free(copy);
    }
}

/* Bytewise classification of valid UTF-8 or retaining a decoder error state
 * loses printable characters after a malformed sequence. */
static void multibyte_display_names(void)
{
    static const struct { const char *input; const char *expected; } cases[] = {
        {"caf\303\251", "caf\303\251"},
        {"\344\270\255", "\344\270\255"},
        {"a\302\205z", "a??z"},
        {"\377\303\251", "?\303\251"},
        {"\342(\241z", "?(?z"},
        {"\342\202", "??"}
    };
    size_t i;
    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL &&
        setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        puts("SKIP UTF-8 cases: no UTF-8 locale available");
        return;
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char *name = format_display_name(cases[i].input, NAME_QMARK);
        assert(name != NULL);
        assert(strcmp(name, cases[i].expected) == 0);
        free(name);
    }
    assert(setlocale(LC_CTYPE, "C") != NULL);
}

/* Executability must not mask type markers or mark non-regular devices. */
static void type_suffixes(void)
{
    static const struct { mode_t mode; char expected; } cases[] = {
        {S_IFDIR | 0755, '/'}, {S_IFLNK | 0777, '@'},
#ifdef S_IFWHT
        {S_IFWHT | 0111, '%'},
#endif
        {S_IFSOCK | 0777, '='}, {S_IFIFO | 0777, '|'},
        {S_IFREG | 0100, '*'}, {S_IFREG | 0010, '*'},
        {S_IFREG | 0001, '*'}, {S_IFREG | 0644, '\0'},
        {S_IFREG | 06000, '\0'},
        {S_IFCHR | 0777, '\0'}, {S_IFBLK | 0777, '\0'}, {0, '\0'}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Entry entry = {.st = {.st_mode = cases[i].mode}};
        /* Synthetic whiteout metadata can be invalid; mode is still usable. */
        assert(format_type_suffix(&entry) == cases[i].expected);
    }
}

static void expect_listing(const EntryList *list, const Options *options,
    const char *expected)
{
    FILE *out = tmpfile();
    char buffer[2048];
    size_t length;
    assert(out != NULL);
    assert(format_listing(out, list, options, true) == 0);
    rewind(out);
    length = fread(buffer, 1, sizeof(buffer), out);
    assert(!ferror(out));
    assert(length == strlen(expected));
    assert(memcmp(buffer, expected, length) == 0);
    assert(fclose(out) == 0);
}

/* Adding markers unconditionally, mutating filesystem strings, or ignoring
 * name_mode changes the public listing or retained entry. */
static void listing_preserves_entries(void)
{
    char name[] = "dir\001";
    char path[] = "parent/dir\001";
    Entry entry = {.name = name, .path = path,
        .st = {.st_mode = S_IFDIR | 0755}, .metadata_valid = true};
    EntryList list = {&entry, 1, 1};
    Options options = {.name_mode = NAME_QMARK, .classify = true};
    expect_listing(&list, &options, "dir?/\n");
    options.name_mode = NAME_RAW;
    expect_listing(&list, &options, "dir\001/\n");
    options.classify = false;
    expect_listing(&list, &options, "dir\001\n");
    assert(entry.name == name && entry.path == path);
    assert(strcmp(name, "dir\001") == 0);
    assert(strcmp(path, "parent/dir\001") == 0);
}

/* Ignoring the TTY default produces raw terminal control bytes or hides
 * bytes when stdout is redirected. No pseudo-terminal is needed. */
static void terminal_default(void)
{
    Entry entry = {.name = "a\001b"};
    EntryList list = {&entry, 1, 1};
    Options options;
    options_init(&options, true, (uid_t)1000, NULL);
    expect_listing(&list, &options, "a?b\n");
    options_init(&options, false, (uid_t)1000, NULL);
    expect_listing(&list, &options, "a\001b\n");
}

/* Swallowing stream errors would incorrectly report successful formatting. */
static void output_error(void)
{
    Entry entry = {.name = "sample"};
    EntryList list = {&entry, 1, 1};
    Options options = {.name_mode = NAME_RAW};
    FILE *out = fopen("/dev/null", "r");
    assert(out != NULL);
    assert(format_listing(out, &list, &options, false) == -1);
    assert(fclose(out) == 0);
}

/* Wrong type precedence or treating special bits as plain execute bits
 * changes these independent ten-character permission expectations. */
static void permission_modes(void)
{
    static const struct { mode_t mode; const char *expected; } cases[] = {
        {S_IFREG | 0644, "-rw-r--r--"},
        {S_IFREG | 0755, "-rwxr-xr-x"},
        {S_IFDIR | 0755, "drwxr-xr-x"},
        {S_IFLNK | 0777, "lrwxrwxrwx"},
        {S_IFBLK | 0600, "brw-------"},
        {S_IFCHR | 0600, "crw-------"},
        {S_IFSOCK | 0700, "srwx------"},
        {S_IFIFO | 0600, "prw-------"},
#ifdef S_IFWHT
        {S_IFWHT, "w---------"},
#endif
        {S_IFREG | 04755, "-rwsr-xr-x"},
        {S_IFREG | 04644, "-rwSr--r--"},
        {S_IFREG | 02755, "-rwxr-sr-x"},
        {S_IFREG | 02644, "-rw-r-Sr--"},
        {S_IFDIR | 01777, "drwxrwxrwt"},
        {S_IFDIR | 01666, "drw-rw-rwT"},
        {S_IFREG | 07777, "-rwsrwsrwt"},
        {0, "?---------"},
        {S_IFREG | 0400, "-r--------"},
        {S_IFREG | 0200, "--w-------"},
        {S_IFREG | 0100, "---x------"},
        {S_IFREG | 0040, "----r-----"},
        {S_IFREG | 0020, "-----w----"},
        {S_IFREG | 0010, "------x---"},
        {S_IFREG | 0004, "-------r--"},
        {S_IFREG | 0002, "--------w-"},
        {S_IFREG | 0001, "---------x"}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char mode[11];
        memset(mode, '!', sizeof(mode));
        format_mode(cases[i].mode, 0, mode);
        assert(mode[10] == '\0');
        assert(strlen(mode) == 10);
        assert(strcmp(mode, cases[i].expected) == 0);
    }
#ifdef DT_WHT
    {
        char mode[11];
        format_mode(0, DT_WHT, mode);
        assert(strcmp(mode, "w---------") == 0);
    }
#endif
}

/* Ignoring long format, changing field order, or measuring only the first
 * entry breaks literal links/ID/size alignment and retained input order. */
static void numeric_long_alignment(void)
{
    Entry entries[] = {
        {.name = "first", .metadata_valid = true,
            .st = {.st_mode = S_IFREG | 0644, .st_nlink = 2,
                .st_uid = 12, .st_gid = 345, .st_size = 7,
                .st_mtim = {.tv_sec = 97440}}},
        {.name = "second", .metadata_valid = true,
            .st = {.st_mode = S_IFREG | 0755, .st_nlink = 15,
                .st_uid = 123, .st_gid = 4, .st_size = 1024,
                .st_mtim = {.tv_sec = 97440}}}
    };
    EntryList list = {entries, 2, 2};
    Options options = {.long_format = true, .numeric_ids = true,
        .name_mode = NAME_RAW};
    expect_listing(&list, &options,
        "-rw-r--r--  2 12  345    7 Jan  2 03:04 first\n"
        "-rwxr-xr-x 15 123 4   1024 Jan  2 03:04 second\n");
    assert(strcmp(entries[0].name, "first") == 0);
    assert(entries[0].st.st_size == 7 && entries[1].st.st_uid == 123);
}

/* Picking mtime for every option or printing a year loses the selected
 * month/day/time contract, even for old timestamps. */
static void selected_long_times(void)
{
    Entry entry = {.name = "sample", .metadata_valid = true,
        .st = {.st_mode = S_IFREG | 0644, .st_nlink = 1,
            .st_mtim = {.tv_sec = 97440},
            .st_ctim = {.tv_sec = 187500},
            .st_atim = {.tv_sec = 277560}}};
    EntryList list = {&entry, 1, 1};
    Options options = {.long_format = true, .numeric_ids = true,
        .name_mode = NAME_RAW, .time_mode = TIME_MTIME};
    expect_listing(&list, &options,
        "-rw-r--r-- 1 0 0 0 Jan  2 03:04 sample\n");
    options.time_mode = TIME_CTIME;
    expect_listing(&list, &options,
        "-rw-r--r-- 1 0 0 0 Jan  3 04:05 sample\n");
    options.time_mode = TIME_ATIME;
    expect_listing(&list, &options,
        "-rw-r--r-- 1 0 0 0 Jan  4 05:06 sample\n");
}

/* Omitting the link target, following it, sanitizing its original bytes,
 * or losing q/w/-F in long output changes these independent expectations. */
static void long_symlink_preserves_target(void)
{
    char name[] = "link\001";
    char target[] = "../absent\001";
    Entry entry = {.name = name, .link_target = target,
        .metadata_valid = true,
        .st = {.st_mode = S_IFLNK | 0777, .st_nlink = 1,
            .st_size = 11, .st_mtim = {.tv_sec = 97440}}};
    EntryList list = {&entry, 1, 1};
    Options options = {.long_format = true, .numeric_ids = true,
        .name_mode = NAME_QMARK, .classify = true};
    expect_listing(&list, &options,
        "lrwxrwxrwx 1 0 0 11 Jan  2 03:04 link?@ -> ../absent\001\n");
    options.name_mode = NAME_RAW;
    expect_listing(&list, &options,
        "lrwxrwxrwx 1 0 0 11 Jan  2 03:04 link\001@ -> ../absent\001\n");
    assert(entry.name == name && entry.link_target == target);
    assert(strcmp(name, "link\001") == 0);
    assert(strcmp(target, "../absent\001") == 0);
}

/* Returning numeric IDs despite a database match, or failing an unknown
 * ID instead of falling back, changes real long-format account fields. */
static void named_ids_and_fallback(void)
{
    Entry entry = {.name = "sample", .metadata_valid = true,
        .st = {.st_mode = S_IFREG | 0644, .st_nlink = 1,
            .st_mtim = {.tv_sec = 97440}}};
    EntryList list = {&entry, 1, 1};
    Options options = {.long_format = true, .name_mode = NAME_RAW};
    struct passwd *owner = getpwuid(0);
    struct group *group;
    char owner_name[1024], group_name[1024], expected[2048];
    int count;
    assert(owner != NULL);
    count = snprintf(owner_name, sizeof(owner_name), "%s", owner->pw_name);
    assert(count >= 0 && (size_t)count < sizeof(owner_name));
    group = getgrgid(0);
    assert(group != NULL);
    count = snprintf(group_name, sizeof(group_name), "%s", group->gr_name);
    assert(count >= 0 && (size_t)count < sizeof(group_name));
    count = snprintf(expected, sizeof(expected),
        "-rw-r--r-- 1 %s %s 0 Jan  2 03:04 sample\n",
        owner_name, group_name);
    assert(count >= 0 && (size_t)count < sizeof(expected));
    expect_listing(&list, &options, expected);

    entry.st.st_uid = (uid_t)-1;
    entry.st.st_gid = (gid_t)-1;
    assert(getpwuid(entry.st.st_uid) == NULL);
    assert(getgrgid(entry.st.st_gid) == NULL);
    count = snprintf(expected, sizeof(expected),
        "-rw-r--r-- 1 %ju %ju 0 Jan  2 03:04 sample\n",
        (uintmax_t)entry.st.st_uid, (uintmax_t)entry.st.st_gid);
    assert(count >= 0 && (size_t)count < sizeof(expected));
    expect_listing(&list, &options, expected);
}

/* Printing st_size for devices or measuring only numeric file sizes loses
 * device identities and shifts timestamps in a mixed listing. */
static void device_size_columns(void)
{
    Entry entries[] = {
        {.name = "block", .metadata_valid = true,
            .st = {.st_mode = S_IFBLK | 0600, .st_nlink = 1,
                .st_size = 987, .st_mtim = {.tv_sec = 97440}}},
        {.name = "character", .metadata_valid = true,
            .st = {.st_mode = S_IFCHR | 0600, .st_nlink = 1,
                .st_size = 987, .st_mtim = {.tv_sec = 97440}}},
        {.name = "file", .metadata_valid = true,
            .st = {.st_mode = S_IFREG | 0644, .st_nlink = 1,
                .st_size = 5, .st_mtim = {.tv_sec = 97440}}}
    };
    EntryList list = {entries, 3, 3};
    Options options = {.long_format = true, .numeric_ids = true,
        .name_mode = NAME_RAW};
    entries[0].st.st_rdev = makedev(12, 345);
    entries[1].st.st_rdev = makedev(1, 2);
    expect_listing(&list, &options,
        "brw------- 1 0 0 12, 345 Jan  2 03:04 block\n"
        "crw------- 1 0 0    1, 2 Jan  2 03:04 character\n"
        "-rw-r--r-- 1 0 0       5 Jan  2 03:04 file\n");
}

/* The long path must propagate write failures and permit empty operands
 * without allocating rows or emitting task-7 directory totals early. */
static void long_empty_and_output_error(void)
{
    Entry entry = {.name = "sample", .metadata_valid = true,
        .st = {.st_mode = S_IFREG | 0644,
            .st_mtim = {.tv_sec = 97440}}};
    EntryList list = {&entry, 1, 1};
    EntryList empty = {0};
    Options options = {.long_format = true, .numeric_ids = true,
        .name_mode = NAME_RAW};
    FILE *out = fopen("/dev/null", "r");
    assert(out != NULL);
    assert(format_listing(out, &list, &options, false) == -1);
    assert(fclose(out) == 0);
    expect_listing(&empty, &options, "");
}

int main(void)
{
    static const struct { const char *name; void (*run)(void); } tests[] = {
        {"display_name_bytes", display_name_bytes},
        {"multibyte_display_names", multibyte_display_names},
        {"type_suffixes", type_suffixes},
        {"listing_preserves_entries", listing_preserves_entries},
        {"terminal_default", terminal_default},
        {"output_error", output_error},
        {"permission_modes", permission_modes},
        {"numeric_long_alignment", numeric_long_alignment},
        {"selected_long_times", selected_long_times},
        {"long_symlink_preserves_target", long_symlink_preserves_target},
        {"named_ids_and_fallback", named_ids_and_fallback},
        {"device_size_columns", device_size_columns},
        {"long_empty_and_output_error", long_empty_and_output_error}
    };
    size_t i;
    assert(setlocale(LC_ALL, "C") != NULL);
    assert(setenv("TZ", "UTC", 1) == 0);
    tzset();
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        tests[i].run();
        printf("PASS %s\n", tests[i].name);
    }
    puts("All 13 formatter tests passed.");
    return 0;
}
