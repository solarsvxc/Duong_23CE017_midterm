#include "format.h"

#include <assert.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

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
    char buffer[128];
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

int main(void)
{
    static const struct { const char *name; void (*run)(void); } tests[] = {
        {"display_name_bytes", display_name_bytes},
        {"multibyte_display_names", multibyte_display_names},
        {"type_suffixes", type_suffixes},
        {"listing_preserves_entries", listing_preserves_entries},
        {"terminal_default", terminal_default},
        {"output_error", output_error}
    };
    size_t i;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        tests[i].run();
        printf("PASS %s\n", tests[i].name);
    }
    puts("All 6 formatter tests passed.");
    return 0;
}
