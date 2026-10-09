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

static void expect_listing_context(const EntryList *list, const Options *options,
    bool directory_contents, const char *expected)
{
    FILE *out = tmpfile();
    char buffer[2048];
    size_t length;
    assert(out != NULL);
    assert(format_listing(out, list, options, directory_contents) == 0);
    rewind(out);
    length = fread(buffer, 1, sizeof(buffer), out);
    assert(!ferror(out));
    assert(length == strlen(expected));
    assert(memcmp(buffer, expected, length) == 0);
    assert(fclose(out) == 0);
}

static void expect_listing(const EntryList *list, const Options *options,
    const char *expected)
{
    expect_listing_context(list, options, false, expected);
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

/* FreeBSD major/minor return signed int: widening directly would turn a
 * high-bit component into a huge uintmax_t rather than its unsigned ID. */
static void high_bit_device_components(void)
{
    Entry entry = {.name = "device", .metadata_valid = true,
        .st = {.st_mode = S_IFCHR | 0600, .st_nlink = 1,
            .st_mtim = {.tv_sec = 97440}}};
    EntryList list = {&entry, 1, 1};
    Options options = {.long_format = true, .numeric_ids = true,
        .name_mode = NAME_RAW};
    entry.st.st_rdev = makedev(0x80000000U, 0xffffffffU);
    expect_listing(&list, &options,
        "crw------- 1 0 0 2147483648, 4294967295 Jan  2 03:04 device\n");
}

/* The long path must propagate write failures and permit empty operands
 * without allocating rows or emitting a directory total. */
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

/* Multiplying first or truncating partial units corrupts these counts. */
static void block_rounding_and_overflow(void)
{
    assert(format_block_units((blkcnt_t)-1, 512) == 0);
    assert(format_block_units(0, 512) == 0);
    assert(format_block_units(1, 512) == 1);
    assert(format_block_units(1, 0) == 1);
    assert(format_block_units(1, 1024) == 1);
    assert(format_block_units(2, 1024) == 1);
    assert(format_block_units(3, 1024) == 2);
    assert(format_block_units(1, 1000) == 1);
    assert(format_block_units(2, 1000) == 2);
    assert(format_block_units(1, UINT64_MAX) == 1);
    assert(format_block_units((blkcnt_t)INT64_MAX, 512) ==
        UINT64_C(9223372036854775807));
    assert(format_block_units((blkcnt_t)INT64_MAX, 1024) ==
        UINT64_C(4611686018427387904));
    assert(format_block_units((blkcnt_t)INT64_MAX, UINT64_MAX) == 256);
    assert(format_block_units((blkcnt_t)INT64_MAX, 1000) ==
        UINT64_C(4722366482869645214));
    assert(format_block_units((blkcnt_t)INT64_MAX, 1) == UINTMAX_MAX);
}

/* Decimal units, missing suffixes, and truncation break exact strings. */
static void human_sizes_and_small_buffers(void)
{
    static const struct { uintmax_t bytes; const char *want; } cases[] = {
        {0, "0B"}, {1023, "1023B"}, {1024, "1.0K"}, {1536, "1.5K"},
        {1048576, "1.0M"}, {1073741824, "1.0G"},
        {UINT64_C(1099511627776), "1.0T"},
        {UINT64_C(1125899906842624), "1.0P"}, {10240, "10K"},
        {UINT64_MAX, "16384P"}
    };
    char buffer[32];
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(format_human_size(cases[i].bytes, buffer, sizeof(buffer)) == 0);
        assert(strcmp(buffer, cases[i].want) == 0);
    }
    assert(format_human_size(1024, buffer, 4) == -1);
    assert(format_human_size(0, buffer, 0) == -1);
    assert(format_human_size(0, NULL, 0) == -1);
}

/* Prefix order and column widths apply equally to short and long rows. */
static void inode_block_prefix_alignment(void)
{
    Entry entries[] = {
        {.name = "first", .st = {.st_ino = 7, .st_blocks = 1,
            .st_mode = S_IFREG | 0644, .st_nlink = 1, .st_size = 1024,
            .st_mtim = {.tv_sec = 97440}}},
        {.name = "second", .st = {.st_ino = 123, .st_blocks = 22,
            .st_mode = S_IFREG | 0644, .st_nlink = 1, .st_size = 3,
            .st_mtim = {.tv_sec = 97440}}}
    };
    EntryList list = {entries, 2, 2};
    Options options = {.show_inode = true, .show_blocks = true,
        .numeric_ids = true, .block_size = 512, .name_mode = NAME_RAW};
    expect_listing(&list, &options, "  7  1 first\n123 22 second\n");
    options.long_format = true;
    options.size_mode = SIZE_KIB;
    expect_listing(&list, &options,
        "  7  1 -rw-r--r-- 1 0 0 1024 Jan  2 03:04 first\n"
        "123 11 -rw-r--r-- 1 0 0    3 Jan  2 03:04 second\n");
    options.size_mode = SIZE_HUMAN;
    expect_listing(&list, &options,
        "  7 512B -rw-r--r-- 1 0 0 1.0K Jan  2 03:04 first\n"
        "123  11K -rw-r--r-- 1 0 0   3B Jan  2 03:04 second\n");
}

/* Rounding each row before summation incorrectly yields total 2. */
static void directory_totals_round_once(void)
{
    Entry entries[] = {
        {.name = "a", .st = {.st_blocks = 1}},
        {.name = "b", .st = {.st_blocks = 1}},
        {.name = "c", .st = {.st_blocks = -1}}
    };
    EntryList list = {entries, 3, 3};
    EntryList empty = {0};
    Options options = {.show_blocks = true, .stdout_is_tty = true,
        .size_mode = SIZE_KIB, .name_mode = NAME_RAW};
    expect_listing_context(&list, &options, true,
        "total 1\n1 a\n1 b\n0 c\n");
    options.size_mode = SIZE_HUMAN;
    expect_listing_context(&list, &options, true,
        "total 1.0K\n512B a\n512B b\n  0B c\n");
    options.long_format = true;
    expect_listing_context(&empty, &options, true, "total 0B\n");
    options.size_mode = SIZE_DEFAULT;
    expect_listing_context(&empty, &options, true, "total 0\n");
}

/* Only -s directory output on a terminal receives the short total. */
static void s_total_only_for_tty_via_formatter(void)
{
    Entry entry = {.name = "sample", .st = {.st_blocks = 1}};
    EntryList list = {&entry, 1, 1};
    Options options = {.show_blocks = true, .block_size = 512,
        .name_mode = NAME_RAW};
    expect_listing_context(&list, &options, true, "1 sample\n");
    options.stdout_is_tty = true;
    expect_listing_context(&list, &options, true, "total 1\n1 sample\n");
    expect_listing_context(&list, &options, false, "1 sample\n");
    options.show_blocks = false;
    expect_listing_context(&list, &options, true, "sample\n");
}

/* Saturating the raw sum prematurely loses a representable final total. */
static void large_totals_keep_raw_precision(void)
{
    Entry entries[] = {
        {.name = "a", .st = {.st_blocks = (blkcnt_t)INT64_MAX}},
        {.name = "b", .st = {.st_blocks = (blkcnt_t)INT64_MAX}},
        {.name = "c", .st = {.st_blocks = (blkcnt_t)INT64_MAX}}
    };
    EntryList list = {entries, 3, 3};
    Options options = {.show_blocks = true, .stdout_is_tty = true,
        .block_size = UINT64_MAX, .name_mode = NAME_RAW};
    expect_listing_context(&list, &options, true,
        "total 768\n256 a\n256 b\n256 c\n");
    options.block_size = 512;
    expect_listing_context(&list, &options, true,
        "total 18446744073709551615\n"
        "9223372036854775807 a\n9223372036854775807 b\n"
        "9223372036854775807 c\n");
}

/* Human mode must preserve devices, including reviewed high-bit IDs. */
static void human_device_size_preserved(void)
{
    Entry entry = {.name = "device", .st = {.st_mode = S_IFCHR | 0600,
        .st_nlink = 1, .st_size = 1536, .st_blocks = 2,
        .st_mtim = {.tv_sec = 97440}}};
    EntryList list = {&entry, 1, 1};
    Options options = {.long_format = true, .numeric_ids = true,
        .show_blocks = true, .size_mode = SIZE_HUMAN, .name_mode = NAME_RAW};
    entry.st.st_rdev = makedev(0x80000000U, 0xffffffffU);
    expect_listing(&list, &options,
        "1.0K crw------- 1 0 0 2147483648, 4294967295 Jan  2 03:04 device\n");
}

/* Long directory totals are required even when stdout is redirected and
 * -s is absent; operand lists must never acquire that total. */
static void long_directory_total_without_s(void)
{
    Entry entry = {.name = "sample", .st = {.st_mode = S_IFREG | 0644,
        .st_nlink = 1, .st_size = 1536, .st_blocks = 3,
        .st_mtim = {.tv_sec = 97440}}};
    EntryList list = {&entry, 1, 1};
    Options options = {.long_format = true, .numeric_ids = true,
        .block_size = 512, .name_mode = NAME_RAW};
    expect_listing_context(&list, &options, true,
        "total 3\n-rw-r--r-- 1 0 0 1536 Jan  2 03:04 sample\n");
    options.size_mode = SIZE_KIB;
    expect_listing_context(&list, &options, true,
        "total 2\n-rw-r--r-- 1 0 0 1536 Jan  2 03:04 sample\n");
    options.size_mode = SIZE_HUMAN;
    expect_listing_context(&list, &options, true,
        "total 1.5K\n-rw-r--r-- 1 0 0 1.5K Jan  2 03:04 sample\n");
    expect_listing(&list, &options,
        "-rw-r--r-- 1 0 0 1.5K Jan  2 03:04 sample\n");
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
        {"high_bit_device_components", high_bit_device_components},
        {"long_empty_and_output_error", long_empty_and_output_error},
        {"block_rounding_and_overflow", block_rounding_and_overflow},
        {"human_sizes_and_small_buffers", human_sizes_and_small_buffers},
        {"inode_block_prefix_alignment", inode_block_prefix_alignment},
        {"directory_totals_round_once", directory_totals_round_once},
        {"s_total_only_for_tty_via_formatter", s_total_only_for_tty_via_formatter},
        {"large_totals_keep_raw_precision", large_totals_keep_raw_precision},
        {"human_device_size_preserved", human_device_size_preserved},
        {"long_directory_total_without_s", long_directory_total_without_s}
    };
    size_t i;
    assert(setlocale(LC_ALL, "C") != NULL);
    assert(setenv("TZ", "UTC", 1) == 0);
    tzset();
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        tests[i].run();
        printf("PASS %s\n", tests[i].name);
    }
    puts("All 22 formatter tests passed.");
    return 0;
}
