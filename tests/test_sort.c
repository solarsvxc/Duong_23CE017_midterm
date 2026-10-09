#include "sort.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void assert_order(Entry *items, size_t length, const Options *options,
    const char *const *expected)
{
    EntryList list = {items, length, length};
    size_t i;
    entry_list_sort(&list, options);
    assert(list.items == items && list.length == length && list.capacity == length);
    for (i = 0; i < length; ++i)
        assert(strcmp(items[i].name, expected[i]) == 0);
}

/* Locale collation, case folding, or leaving input untouched breaks this order. */
static void name_order(void)
{
    Entry items[] = {{.name = "z"}, {.name = "a"}, {.name = "A"},
        {.name = ".dot"}};
    const char *expected[] = {".dot", "A", "a", "z"};
    Options options = {.sort_mode = SORT_NAME};
    assert_order(items, 4, &options, expected);
    options.reverse = true;
    {
        const char *reversed[] = {"z", "a", "A", ".dot"};
        assert_order(items, 4, &options, reversed);
    }
}

/* Ascending size or subtraction narrowed to int fails the wide signed keys. */
static void size_descending(void)
{
    Entry items[] = {{.name = "middle", .st = {.st_size = 1}},
        {.name = "small", .st = {.st_size = -1}},
        {.name = "large", .st = {.st_size = INT64_MAX}}};
    const char *expected[] = {"large", "middle", "small"};
    Options options = {.sort_mode = SORT_SIZE};
    assert_order(items, 3, &options, expected);
    options.reverse = true;
    {
        const char *reversed[] = {"small", "middle", "large"};
        assert_order(items, 3, &options, reversed);
    }
}

/* Selecting another clock or sorting oldest first breaks this fixture. */
static void mtime_descending(void)
{
    Entry items[] = {{.name = "middle", .st = {.st_mtim = {1, 0}}},
        {.name = "old", .st = {.st_mtim = {-1, 0}}},
        {.name = "new", .st = {.st_mtim = {INT64_MAX, 0}}}};
    const char *expected[] = {"new", "middle", "old"};
    Options options = {.sort_mode = SORT_TIME, .time_mode = TIME_MTIME};
    assert_order(items, 3, &options, expected);
    options.reverse = true;
    {
        const char *reversed[] = {"old", "middle", "new"};
        assert_order(items, 3, &options, reversed);
    }
}

/* Ignoring time_mode or retaining the last call's context changes these orders. */
static void ctime_and_atime_selection(void)
{
    Entry items[] = {
        {.name = "a", .st = {.st_mtim = {3, 0}, .st_ctim = {1, 0},
            .st_atim = {2, 0}}},
        {.name = "b", .st = {.st_mtim = {2, 0}, .st_ctim = {3, 0},
            .st_atim = {1, 0}}},
        {.name = "c", .st = {.st_mtim = {1, 0}, .st_ctim = {2, 0},
            .st_atim = {3, 0}}}
    };
    const char *ctime_expected[] = {"b", "c", "a"};
    const char *atime_expected[] = {"c", "a", "b"};
    Options options = {.sort_mode = SORT_TIME, .time_mode = TIME_CTIME};
    assert_order(items, 3, &options, ctime_expected);
    options.time_mode = TIME_ATIME;
    assert_order(items, 3, &options, atime_expected);
}

/* Ignoring nanoseconds or comparing them before seconds breaks each clock. */
static void subsecond_time_order(void)
{
    TimeMode modes[] = {TIME_MTIME, TIME_CTIME, TIME_ATIME};
    size_t i;
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        Entry items[] = {
            {.name = "a", .st = {.st_mtim = {10, 1}, .st_ctim = {10, 1},
                .st_atim = {10, 1}}},
            {.name = "b", .st = {.st_mtim = {10, 999999999},
                .st_ctim = {10, 999999999}, .st_atim = {10, 999999999}}},
            {.name = "c", .st = {.st_mtim = {11, 0}, .st_ctim = {11, 0},
                .st_atim = {11, 0}}}
        };
        const char *expected[] = {"c", "b", "a"};
        Options options = {.sort_mode = SORT_TIME, .time_mode = modes[i]};
        assert_order(items, 3, &options, expected);
    }
}

/* Returning equality for equal primary keys loses deterministic name order. */
static void equal_key_name_tiebreak(void)
{
    SortMode modes[] = {SORT_SIZE, SORT_TIME};
    size_t i;
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        Entry items[] = {{.name = "c"}, {.name = "a"}, {.name = "b"}};
        const char *expected[] = {"a", "b", "c"};
        Options options = {.sort_mode = modes[i], .time_mode = TIME_MTIME};
        assert_order(items, 3, &options, expected);
    }
}

/* Reversing only the primary key leaves the tied group in the wrong order. */
static void reverse_reverses_tiebreak(void)
{
    SortMode modes[] = {SORT_SIZE, SORT_TIME};
    size_t i;
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        Entry items[] = {
            {.name = "b", .st = {.st_size = 2, .st_mtim = {2, 0}}},
            {.name = "a", .st = {.st_size = 2, .st_mtim = {2, 0}}},
            {.name = "c", .st = {.st_size = 1, .st_mtim = {1, 0}}}
        };
        const char *expected[] = {"c", "b", "a"};
        Options options = {.sort_mode = modes[i], .time_mode = TIME_MTIME,
            .reverse = true};
        assert_order(items, 3, &options, expected);
    }
}

/* Sorting or applying reverse despite SORT_NONE changes the input sequence. */
static void none_preserves_input_order(void)
{
    Entry items[] = {{.name = "b"}, {.name = "c"}, {.name = "a"}};
    const char *expected[] = {"b", "c", "a"};
    Options options = {.sort_mode = SORT_NONE};
    assert_order(items, 3, &options, expected);
    options.reverse = true;
    assert_order(items, 3, &options, expected);
}

/* Empty lists may have NULL storage; singleton entries must remain intact. */
static void empty_and_singleton(void)
{
    Entry item = {.name = "only"};
    const char *expected[] = {"only"};
    Options options = {.sort_mode = SORT_NAME, .reverse = true};
    assert_order(NULL, 0, &options, NULL);
    assert_order(&item, 1, &options, expected);
}

int main(void)
{
    static const struct { const char *name; void (*run)(void); } tests[] = {
        {"name_order", name_order},
        {"size_descending", size_descending},
        {"mtime_descending", mtime_descending},
        {"ctime_and_atime_selection", ctime_and_atime_selection},
        {"subsecond_time_order", subsecond_time_order},
        {"equal_key_name_tiebreak", equal_key_name_tiebreak},
        {"reverse_reverses_tiebreak", reverse_reverses_tiebreak},
        {"none_preserves_input_order", none_preserves_input_order},
        {"empty_and_singleton", empty_and_singleton}
    };
    size_t i;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        tests[i].run();
        printf("PASS %s\n", tests[i].name);
    }
    puts("All 9 sorting tests passed.");
    return 0;
}
