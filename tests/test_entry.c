#include "entry.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Test only: fail each allocation in the real collector and count ownership. */
static size_t fail_after = SIZE_MAX;
static size_t live_allocations;

static bool allocation_fails(void)
{
    if (fail_after == SIZE_MAX)
        return false;
    if (fail_after == 0) {
        errno = ENOMEM;
        return true;
    }
    --fail_after;
    return false;
}

static void *checked_malloc(size_t size)
{
    void *result;
    if (allocation_fails())
        return NULL;
    result = malloc(size);
    if (result != NULL)
        ++live_allocations;
    return result;
}

static void *checked_realloc(void *pointer, size_t size)
{
    bool was_null = pointer == NULL;
    void *result;
    assert(size > 0);
    if (allocation_fails())
        return NULL;
    result = realloc(pointer, size);
    if (result != NULL && was_null)
        ++live_allocations;
    return result;
}

static void checked_free(void *pointer)
{
    if (pointer != NULL) {
        assert(live_allocations > 0);
        --live_allocations;
    }
    free(pointer);
}

/* Compile the unchanged module with local allocator substitution. The normal
 * src/entry.o is also a prerequisite, so its standalone build is checked. */
#define malloc checked_malloc
#define realloc checked_realloc
#define free checked_free
#include "../src/entry.c"
#undef free
#undef realloc
#undef malloc

static char *fixture_path(const char *directory, const char *name)
{
    size_t size = strlen(directory) + strlen(name) + 2;
    char *result = malloc(size);
    assert(result != NULL);
    assert(snprintf(result, size, "%s/%s", directory, name) > 0);
    return result;
}

static void create_file(const char *path)
{
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs("data", file) >= 0);
    assert(fclose(file) == 0);
}

static char *fixture_create(void)
{
    char template[] = "/tmp/my_ls_entry.XXXXXX";
    char *root, *path;
    assert(mkdtemp(template) != NULL);
    root = fixture_path(template, "");
    root[strlen(root) - 1] = '\0';
    path = fixture_path(root, "visible");
    create_file(path);
    free(path);
    path = fixture_path(root, ".hidden");
    create_file(path);
    free(path);
    path = fixture_path(root, "folder");
    assert(mkdir(path, 0700) == 0);
    free(path);
    return root;
}

static void fixture_destroy(char *root)
{
    const char *names[] = {"visible", ".hidden", "folder"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char *path = fixture_path(root, names[i]);
        if (i == 2)
            assert(rmdir(path) == 0);
        else
            assert(unlink(path) == 0);
        free(path);
    }
    assert(rmdir(root) == 0);
    free(root);
}

static const Entry *find_entry(const EntryList *list, const char *name)
{
    size_t i;
    for (i = 0; i < list->length; ++i)
        if (strcmp(list->items[i].name, name) == 0)
            return &list->items[i];
    return NULL;
}

/* Following links during collection, truncating targets, or borrowing input
 * strings breaks the retained lstat metadata and owned-string assertions. */
static void metadata_and_symlink_target(void)
{
    EntryList list;
    char *root = fixture_create();
    char *path = fixture_path(root, "visible");
    char name[] = "visible";
    char *link = fixture_path(root, "link");
    char *dangling = fixture_path(root, "dangling");
    char *long_link = fixture_path(root, "longlink");
    char target[401];
    const Entry *entry;
    memset(target, 'x', sizeof(target) - 1);
    target[sizeof(target) - 1] = '\0';
    assert(symlink("folder", link) == 0);
    assert(symlink("missing", dangling) == 0);
    assert(symlink(target, long_link) == 0);
    entry_list_init(&list);
    assert(entry_list_add_path(&list, name, path, DT_UNKNOWN, true, stderr) == 0);
    name[0] = 'X';
    path[0] = 'X';
    assert(strcmp(list.items[0].name, "visible") == 0);
    assert(list.items[0].path[0] == '/');
    path[0] = '/';
    assert(strcmp(list.items[0].path, path) == 0);
    assert(list.items[0].metadata_valid);
    assert(S_ISREG(list.items[0].st.st_mode));
    assert(list.items[0].st.st_size == 4);
    assert(list.items[0].link_target == NULL);
    assert(list.items[0].dirent_type == DT_UNKNOWN);
    assert(list.items[0].is_operand);
    assert(!entry_operand_is_directory(&list.items[0], false));
    assert(entry_list_add_path(&list, "link", link, DT_UNKNOWN, true, stderr) == 0);
    entry = find_entry(&list, "link");
    assert(entry != NULL && entry->metadata_valid);
    assert(S_ISLNK(entry->st.st_mode));
    assert(strcmp(entry->link_target, "folder") == 0);
    assert(entry_operand_is_directory(entry, false));
    assert(!entry_operand_is_directory(entry, true));
    assert(S_ISLNK(entry->st.st_mode));
    assert(entry_list_add_path(&list, "child-link", link, DT_LNK, false, stderr) == 0);
    assert(!entry_operand_is_directory(find_entry(&list, "child-link"), false));
    assert(entry_list_add_path(&list, "dangling", dangling, DT_UNKNOWN, true, stderr) == 0);
    assert(!entry_operand_is_directory(find_entry(&list, "dangling"), false));
    assert(entry_list_add_path(&list, "longlink", long_link, DT_LNK, false, stderr) == 0);
    assert(strcmp(find_entry(&list, "longlink")->link_target, target) == 0);
    entry_list_destroy(&list);
    assert(live_allocations == 0);
    assert(unlink(link) == 0);
    assert(unlink(dangling) == 0);
    assert(unlink(long_link) == 0);
    free(link);
    free(dangling);
    free(long_link);
    free(path);
    fixture_destroy(root);
}

static void check_hidden_policy(bool all, bool almost, size_t expected)
{
    EntryList list;
    Options options;
    char *root = fixture_create();
    const Entry *folder;
    options_init(&options, false, (uid_t)1000, NULL);
    options.show_all = all;
    options.almost_all = almost;
    entry_list_init(&list);
    assert(entry_list_read_directory(&list, root, &options, stderr) == 0);
    assert(list.length == expected);
    assert(find_entry(&list, "visible") != NULL);
    assert((find_entry(&list, ".hidden") != NULL) == (all || almost));
    assert((find_entry(&list, ".") != NULL) == all);
    assert((find_entry(&list, "..") != NULL) == all);
    folder = find_entry(&list, "folder");
    assert(folder != NULL && !folder->is_operand);
    assert(entry_operand_is_directory(folder, true));
    entry_list_destroy(&list);
    assert(live_allocations == 0);
    fixture_destroy(root);
}

/* Wrong hidden-policy branches fail the three independently sized fixtures. */
static void default_hides_dot_names(void) { check_hidden_policy(false, false, 2); }
static void almost_all_excludes_dot_entries(void) { check_hidden_policy(false, true, 3); }
static void all_includes_dot_entries(void) { check_hidden_policy(true, true, 5); }

/* Missing metadata must report its path and must not insert a fake entry. */
static void missing_path_returns_error(void)
{
    EntryList list;
    Options options;
    char *root = fixture_create();
    char *missing = fixture_path(root, "missing");
    char diagnostic[1024];
    FILE *err = tmpfile();
    size_t length;
    assert(err != NULL);
    entry_list_init(&list);
    options_init(&options, false, (uid_t)1000, NULL);
    assert(entry_list_add_path(&list, "missing", missing, DT_UNKNOWN, true, err) != 0);
    assert(list.length == 0);
    assert(entry_list_read_directory(&list, missing, &options, err) != 0);
    rewind(err);
    length = fread(diagnostic, 1, sizeof(diagnostic) - 1, err);
    diagnostic[length] = '\0';
    assert(strstr(diagnostic, "my_ls:") != NULL);
    assert(strstr(diagnostic, missing) != NULL);
    assert(strstr(diagnostic, strerror(ENOENT)) != NULL);
    assert(fclose(err) == 0);
    entry_list_destroy(&list);
    assert(live_allocations == 0);
    free(missing);
    fixture_destroy(root);
}

/* Discarding a dirent-reported whiteout on lstat failure loses that name. */
static void conditional_whiteout_without_lstat(void)
{
#if defined(DT_WHT) && defined(S_IFWHT)
    EntryList list;
    char *root = fixture_create();
    char *missing = fixture_path(root, "whiteout");
    FILE *err = tmpfile();
    assert(err != NULL);
    entry_list_init(&list);
    assert(entry_list_add_path(&list, "whiteout", missing, DT_WHT, false, err) == 0);
    assert(list.length == 1);
    assert(!list.items[0].metadata_valid);
    assert((list.items[0].st.st_mode & S_IFMT) == S_IFWHT);
    assert(list.items[0].dirent_type == DT_WHT);
    assert(!entry_operand_is_directory(&list.items[0], false));
    assert(list.items[0].link_target == NULL);
    assert(ftell(err) == 0);
    assert(fclose(err) == 0);
    entry_list_destroy(&list);
    assert(live_allocations == 0);
    free(missing);
    fixture_destroy(root);
#else
    puts("SKIP whiteout branch: DT_WHT/S_IFWHT unavailable");
#endif
}

/* Fail every real allocation, including target/list growth; failed additions
 * must preserve existing entries and leave no unowned temporary strings. */
static void partial_failure_cleans_up(void)
{
    char *root = fixture_create();
    char *visible = fixture_path(root, "visible");
    char *link = fixture_path(root, "longlink");
    char target[401];
    size_t failure;
    bool success = false;
    memset(target, 'x', sizeof(target) - 1);
    target[sizeof(target) - 1] = '\0';
    assert(symlink(target, link) == 0);
    for (failure = 0; failure < 64; ++failure) {
        EntryList list;
        size_t length, baseline_allocations;
        int result;
        FILE *err = tmpfile();
        assert(err != NULL);
        entry_list_init(&list);
        assert(entry_list_add_path(&list, "visible", visible, DT_UNKNOWN, true, err) == 0);
        while (list.length < list.capacity)
            assert(entry_list_add_path(&list, "visible", visible, DT_UNKNOWN, true, err) == 0);
        length = list.length;
        baseline_allocations = live_allocations;
        fail_after = failure;
        result = entry_list_add_path(&list, "longlink", link, DT_UNKNOWN, true, err);
        fail_after = SIZE_MAX;
        assert(strcmp(list.items[0].path, visible) == 0);
        if (result != 0) {
            assert(list.length == length);
            assert(live_allocations == baseline_allocations);
            assert(ftell(err) > 0);
        } else {
            assert(list.length == length + 1);
            assert(strcmp(list.items[length].link_target, target) == 0);
            success = true;
        }
        entry_list_destroy(&list);
        entry_list_destroy(&list);
        assert(list.items == NULL && list.length == 0 && list.capacity == 0);
        assert(live_allocations == 0);
        assert(fclose(err) == 0);
        if (success)
            break;
    }
    assert(success);
    success = false;
    for (failure = 0; failure < 64; ++failure) {
        EntryList list;
        Options options;
        FILE *err = tmpfile();
        int result;
        assert(err != NULL);
        options_init(&options, false, (uid_t)1000, NULL);
        entry_list_init(&list);
        assert(entry_list_add_path(&list, "seed", visible, DT_UNKNOWN, true, err) == 0);
        fail_after = failure;
        result = entry_list_read_directory(&list, root, &options, err);
        fail_after = SIZE_MAX;
        assert(list.length >= 1 && strcmp(list.items[0].name, "seed") == 0);
        success = result == 0;
        if (!success)
            assert(ftell(err) > 0);
        entry_list_destroy(&list);
        assert(live_allocations == 0);
        assert(fclose(err) == 0);
        if (success)
            break;
    }
    assert(success);
    assert(unlink(link) == 0);
    free(link);
    free(visible);
    fixture_destroy(root);
}

/* Fixed or truncated path storage fails an exact comparison at 800+ bytes. */
static void long_nested_path(void)
{
    enum { DEPTH = 8 };
    char *root = fixture_create();
    char *directories[DEPTH];
    char component[97];
    char *path;
    EntryList list;
    Options options;
    size_t i;
    memset(component, 'n', sizeof(component) - 1);
    component[sizeof(component) - 1] = '\0';
    for (i = 0; i < DEPTH; ++i) {
        directories[i] = fixture_path(i == 0 ? root : directories[i - 1], component);
        assert(mkdir(directories[i], 0700) == 0);
    }
    path = fixture_path(directories[DEPTH - 1], "leaf");
    assert(strlen(path) > 800);
    create_file(path);
    options_init(&options, false, (uid_t)1000, NULL);
    entry_list_init(&list);
    assert(entry_list_add_path(&list, "leaf", path, DT_UNKNOWN, true, stderr) == 0);
    assert(strcmp(list.items[0].path, path) == 0);
    assert(entry_list_read_directory(&list, directories[DEPTH - 1], &options, stderr) == 0);
    assert(list.length == 2);
    assert(strcmp(list.items[1].path, path) == 0);
    entry_list_destroy(&list);
    assert(live_allocations == 0);
    assert(unlink(path) == 0);
    free(path);
    for (i = DEPTH; i > 0; --i) {
        assert(rmdir(directories[i - 1]) == 0);
        free(directories[i - 1]);
    }
    fixture_destroy(root);
}

/* Sorting collection output or losing d_type breaks the independent scan. */
static void readdir_order_and_trailing_slash(void)
{
    char *root = fixture_create();
    char *with_slash = fixture_path(root, "");
    DIR *directory = opendir(root);
    struct dirent *item;
    EntryList list;
    Options options;
    size_t index = 0;
    assert(directory != NULL);
    options_init(&options, false, (uid_t)1000, NULL);
    options.show_all = true;
    entry_list_init(&list);
    assert(entry_list_read_directory(&list, with_slash, &options, stderr) == 0);
    while ((item = readdir(directory)) != NULL) {
        char *expected = fixture_path(root, item->d_name);
        assert(index < list.length);
        assert(strcmp(list.items[index].name, item->d_name) == 0);
        assert(strcmp(list.items[index].path, expected) == 0);
        assert(list.items[index].dirent_type == item->d_type);
        assert(!list.items[index].is_operand);
        free(expected);
        ++index;
    }
    assert(index == list.length);
    assert(closedir(directory) == 0);
    entry_list_destroy(&list);
    assert(live_allocations == 0);
    free(with_slash);
    fixture_destroy(root);
}

int main(void)
{
    static const struct { const char *name; void (*run)(void); } tests[] = {
        {"metadata_and_symlink_target", metadata_and_symlink_target},
        {"default_hides_dot_names", default_hides_dot_names},
        {"almost_all_excludes_dot_entries", almost_all_excludes_dot_entries},
        {"all_includes_dot_entries", all_includes_dot_entries},
        {"missing_path_returns_error", missing_path_returns_error},
        {"conditional_whiteout_without_lstat", conditional_whiteout_without_lstat},
        {"partial_failure_cleans_up", partial_failure_cleans_up},
        {"long_nested_path", long_nested_path},
        {"readdir_order_and_trailing_slash", readdir_order_and_trailing_slash}
    };
    size_t i;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        tests[i].run();
        printf("PASS %s\n", tests[i].name);
    }
    puts("All 9 entry tests passed.");
    return 0;
}
