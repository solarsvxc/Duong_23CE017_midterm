#include "options.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static Options baseline(bool tty, uid_t uid)
{
    Options result = {0};
    result.almost_all = uid == 0;
    result.stdout_is_tty = tty;
    result.sort_mode = SORT_NAME;
    result.time_mode = TIME_MTIME;
    result.name_mode = tty ? NAME_QMARK : NAME_RAW;
    result.size_mode = SIZE_DEFAULT;
    result.block_size = 512;
    return result;
}

static void same_options(const Options *actual, const Options *expected)
{
#define CHECK(field) assert(actual->field == expected->field)
    CHECK(show_all); CHECK(almost_all); CHECK(directory_as_file);
    CHECK(classify); CHECK(show_inode); CHECK(show_blocks);
    CHECK(recursive); CHECK(reverse); CHECK(long_format); CHECK(numeric_ids);
    CHECK(stdout_is_tty); CHECK(sort_mode); CHECK(time_mode);
    CHECK(name_mode); CHECK(size_mode); CHECK(block_size);
#undef CHECK
}

static Options parse_flags(const char *flags)
{
    Options result;
    char *argv[] = {"my_ls", (char *)flags, "operand", NULL};
    int index = -1;
    options_init(&result, false, (uid_t)1000, NULL);
    assert(options_parse(&result, 3, argv, &index, stderr) == 0);
    assert(index == 2);
    return result;
}

/* Removing terminal-dependent initialization must fail this table. */
static void defaults(void)
{
    const bool terminals[] = {false, true};
    size_t i;
    for (i = 0; i < sizeof(terminals) / sizeof(terminals[0]); ++i) {
        Options actual, expected = baseline(terminals[i], (uid_t)1000);
        char *argv[] = {"my_ls", NULL};
        int index = -1;
        options_init(&actual, terminals[i], (uid_t)1000, NULL);
        assert(options_parse(&actual, 1, argv, &index, stderr) == 0);
        assert(index == 1);
        same_options(&actual, &expected);
    }
}

/* Missing handlers or unintended side effects fail full-state comparisons. */
static void all_supported_flags(void)
{
    static const struct { char flag; Options expected; } cases[] = {
        {'A', {.almost_all = true, .name_mode = NAME_RAW, .block_size = 512}},
        {'a', {.show_all = true, .name_mode = NAME_RAW, .block_size = 512}},
        {'c', {.time_mode = TIME_CTIME, .name_mode = NAME_RAW, .block_size = 512}},
        {'d', {.directory_as_file = true, .name_mode = NAME_RAW, .block_size = 512}},
        {'F', {.classify = true, .name_mode = NAME_RAW, .block_size = 512}},
        {'f', {.sort_mode = SORT_NONE, .name_mode = NAME_RAW, .block_size = 512}},
        {'h', {.size_mode = SIZE_HUMAN, .name_mode = NAME_RAW, .block_size = 512}},
        {'i', {.show_inode = true, .name_mode = NAME_RAW, .block_size = 512}},
        {'k', {.size_mode = SIZE_KIB, .name_mode = NAME_RAW, .block_size = 512}},
        {'l', {.long_format = true, .name_mode = NAME_RAW, .block_size = 512}},
        {'n', {.long_format = true, .numeric_ids = true,
            .name_mode = NAME_RAW, .block_size = 512}},
        {'q', {.name_mode = NAME_QMARK, .block_size = 512}},
        {'R', {.recursive = true, .name_mode = NAME_RAW, .block_size = 512}},
        {'r', {.reverse = true, .name_mode = NAME_RAW, .block_size = 512}},
        {'S', {.sort_mode = SORT_SIZE, .name_mode = NAME_RAW, .block_size = 512}},
        {'s', {.show_blocks = true, .name_mode = NAME_RAW, .block_size = 512}},
        {'t', {.sort_mode = SORT_TIME, .name_mode = NAME_RAW, .block_size = 512}},
        {'u', {.time_mode = TIME_ATIME, .name_mode = NAME_RAW, .block_size = 512}},
        {'w', {.name_mode = NAME_RAW, .block_size = 512}}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char argument[] = {'-', cases[i].flag, '\0'};
        Options actual = parse_flags(argument);
        same_options(&actual, &cases[i].expected);
    }
}

/* A first-wins parser or failure to clear the opposing flag fails here. */
static void rightmost_pairs(void)
{
    static const struct {
        const char *flags;
        bool numeric, directory, recursive;
        TimeMode time;
        SizeMode size;
        NameMode name;
    } cases[] = {
        {"-ln", true, false, false, TIME_MTIME, SIZE_DEFAULT, NAME_RAW},
        {"-nl", false, false, false, TIME_MTIME, SIZE_DEFAULT, NAME_RAW},
        {"-cu", false, false, false, TIME_ATIME, SIZE_DEFAULT, NAME_RAW},
        {"-uc", false, false, false, TIME_CTIME, SIZE_DEFAULT, NAME_RAW},
        {"-Rd", false, true, false, TIME_MTIME, SIZE_DEFAULT, NAME_RAW},
        {"-dR", false, false, true, TIME_MTIME, SIZE_DEFAULT, NAME_RAW},
        {"-hk", false, false, false, TIME_MTIME, SIZE_KIB, NAME_RAW},
        {"-kh", false, false, false, TIME_MTIME, SIZE_HUMAN, NAME_RAW},
        {"-qw", false, false, false, TIME_MTIME, SIZE_DEFAULT, NAME_RAW},
        {"-wq", false, false, false, TIME_MTIME, SIZE_DEFAULT, NAME_QMARK}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Options actual = parse_flags(cases[i].flags);
        Options expected = baseline(false, (uid_t)1000);
        expected.long_format = i < 2;
        expected.numeric_ids = cases[i].numeric;
        expected.directory_as_file = cases[i].directory;
        expected.recursive = cases[i].recursive;
        expected.time_mode = cases[i].time;
        expected.size_mode = cases[i].size;
        expected.name_mode = cases[i].name;
        same_options(&actual, &expected);
    }
    {
        Options actual;
        Options expected = baseline(false, (uid_t)1000);
        char *argv[] = {"my_ls", "-nucRhq", "-ldkw", NULL};
        int index = -1;
        options_init(&actual, false, (uid_t)1000, NULL);
        assert(options_parse(&actual, 3, argv, &index, stderr) == 0);
        assert(index == 3);
        expected.long_format = true;
        expected.time_mode = TIME_CTIME;
        expected.directory_as_file = true;
        expected.size_mode = SIZE_KIB;
        same_options(&actual, &expected);
    }
}

static void sort_selector_rightmost(void)
{
    static const struct { const char *flags; SortMode sort; bool reverse; } cases[] = {
        {"-fSt", SORT_TIME, false}, {"-ftS", SORT_SIZE, false},
        {"-Stf", SORT_NONE, false}, {"-rSf", SORT_NONE, true},
        {"-fSr", SORT_SIZE, true}, {"-tfrt", SORT_TIME, true}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Options actual = parse_flags(cases[i].flags);
        Options expected = baseline(false, (uid_t)1000);
        expected.sort_mode = cases[i].sort;
        expected.reverse = cases[i].reverse;
        same_options(&actual, &expected);
    }
}

static void root_enables_almost_all(void)
{
    const uid_t ids[] = {0, 1000};
    size_t i;
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        Options actual, expected = baseline(false, ids[i]);
        char *argv[] = {"my_ls", "-aA", NULL};
        int index;
        options_init(&actual, false, ids[i], NULL);
        same_options(&actual, &expected);
        assert(options_parse(&actual, 2, argv, &index, stderr) == 0);
        expected.show_all = true;
        expected.almost_all = true;
        same_options(&actual, &expected);
    }
}

static void double_dash_stops_options(void)
{
    static const struct { int argc; char *argv[5]; int index; bool all; } cases[] = {
        {4, {"my_ls", "-a", "--", "-z", NULL}, 3, true},
        {3, {"my_ls", "operand", "-a", NULL, NULL}, 1, false},
        {3, {"my_ls", "-", "-a", NULL, NULL}, 1, false},
        {2, {"my_ls", "--", NULL, NULL, NULL}, 2, false}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Options actual, expected = baseline(false, (uid_t)1000);
        int index = -1;
        options_init(&actual, false, (uid_t)1000, NULL);
        assert(options_parse(&actual, cases[i].argc, cases[i].argv, &index, stderr) == 0);
        assert(index == cases[i].index);
        expected.show_all = cases[i].all;
        same_options(&actual, &expected);
    }
}

static void unknown_option_reports_usage(void)
{
    const char *invalid[] = {"-z", "--help", "-az"};
    size_t i;
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        Options actual;
        char *argv[] = {"my_ls", (char *)invalid[i], NULL};
        char diagnostic[512];
        int index = -1;
        size_t length;
        FILE *err = tmpfile();
        assert(err != NULL);
        options_init(&actual, false, (uid_t)1000, NULL);
        assert(options_parse(&actual, 2, argv, &index, err) != 0);
        assert(index == 1);
        rewind(err);
        length = fread(diagnostic, 1, sizeof(diagnostic) - 1, err);
        diagnostic[length] = '\0';
        assert(strstr(diagnostic, "usage:") != NULL);
        assert(strstr(diagnostic, "[-AacdFfhiklnqRrSstuw]") != NULL);
        assert(strstr(diagnostic, "illegal option") != NULL);
        assert(fclose(err) == 0);
    }
}

static void blocksize_table(void)
{
    static const struct { const char *input; uint64_t expected; } cases[] = {
        {NULL, 512}, {"", 512}, {"invalid", 512}, {"0", 512},
        {"18446744073709551616", 512}, {"18446744073709551615K", 512},
        {"2K", 2048}, {"1M", 1048576}, {"1024", 1024},
        {"1G", 1073741824}, {"1T", UINT64_C(1099511627776)},
        {"1P", UINT64_C(1125899906842624)}, {"-1", 512},
        {"+1", 512}, {" 2K", 512}, {"2KB", 512}, {"2Kx", 512},
        {"0K", 512}, {"18446744073709551615", UINT64_MAX}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Options actual, expected = baseline(false, (uid_t)1000);
        options_init(&actual, false, (uid_t)1000, cases[i].input);
        expected.block_size = cases[i].expected;
        same_options(&actual, &expected);
    }
}

int main(void)
{
    static const struct { const char *name; void (*run)(void); } tests[] = {
        {"defaults", defaults}, {"all_supported_flags", all_supported_flags},
        {"rightmost_pairs", rightmost_pairs},
        {"sort_selector_rightmost", sort_selector_rightmost},
        {"root_enables_almost_all", root_enables_almost_all},
        {"double_dash_stops_options", double_dash_stops_options},
        {"unknown_option_reports_usage", unknown_option_reports_usage},
        {"blocksize_table", blocksize_table}
    };
    size_t i;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        tests[i].run();
        printf("PASS %s\n", tests[i].name);
    }
    puts("All 8 option tests passed.");
    return 0;
}
