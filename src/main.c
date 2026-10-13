#include "entry.h"
#include "format.h"
#include "options.h"
#include "sort.h"

#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Stable partition without collecting or duplicating any path twice.
 * The owner retains every entry's strings. The two views borrow its array
 * and must never be passed to entry_list_destroy. */
static int group_operands(EntryList *owner, const Options *options,
    EntryList *files, EntryList *directories)
{
    Entry *grouped;
    size_t i, file_count = 0, directory_start = owner->length;
    size_t left, right;

    entry_list_init(files);
    entry_list_init(directories);
    if (owner->length == 0)
        return 0;
    if (owner->length > SIZE_MAX / sizeof(*grouped)) {
        errno = EOVERFLOW;
        return -1;
    }
    grouped = malloc(owner->length * sizeof(*grouped));
    if (grouped == NULL)
        return -1;
    for (i = 0; i < owner->length; ++i) {
        bool is_directory = !options->directory_as_file &&
            entry_operand_is_directory(&owner->items[i], false);
        if (is_directory)
            grouped[--directory_start] = owner->items[i];
        else
            grouped[file_count++] = owner->items[i];
    }
    /* Directory entries were appended from the far end; restore input order
     * before sorting so SORT_NONE keeps each group's original order. */
    left = directory_start;
    right = owner->length;
    while (left < right) {
        Entry temporary;
        --right;
        if (left >= right)
            break;
        temporary = grouped[left];
        grouped[left++] = grouped[right];
        grouped[right] = temporary;
    }
    free(owner->items);
    owner->items = grouped;
    owner->capacity = owner->length;
    files->items = grouped;
    files->length = file_count;
    files->capacity = file_count;
    directories->items = grouped + directory_start;
    directories->length = owner->length - directory_start;
    directories->capacity = directories->length;
    return 0;
}

static void report_format_error(const char *path, bool *had_error)
{
    int error = errno;
    fprintf(stderr, "my_ls: %s: formatting: %s\n", path, strerror(error));
    *had_error = true;
}

static void print_listing(const EntryList *list, const Options *options,
    bool directory_contents, const char *path, bool *had_error,
    bool *output_failed)
{
    const char *error_path;
    if (format_listing(stdout, list, options, directory_contents,
        &error_path, NULL) == 0)
        return;
    if (ferror(stdout))
        *output_failed = true;
    else
        report_format_error(error_path == NULL ? path : error_path, had_error);
}

/* Keep the usual aligned group output. If formatting one file fails, retry
 * only entries that were not written, skipping that file and continuing with
 * later operands. A group-wide allocation failure gets a singleton fallback. */
static void print_file_operands(const EntryList *files, const Options *options,
    bool *had_error, bool *output_failed)
{
    size_t start = 0;

    while (start < files->length && !*output_failed) {
        EntryList remaining = {files->items + start,
            files->length - start, files->length - start};
        FormatProgress progress;
        const char *error_path;
        size_t i;

        if (format_listing(stdout, &remaining, options, false,
            &error_path, &progress) == 0)
            return;
        if (ferror(stdout)) {
            *output_failed = true;
            return;
        }
        report_format_error(error_path == NULL ? remaining.items[0].path :
            error_path, had_error);

        if (progress.failed_index == SIZE_MAX) {
            /* No entry was written: the long-field array could not be
             * allocated. Try each independent operand once. */
            for (i = start; i < files->length && !*output_failed; ++i) {
                EntryList single = {files->items + i, 1, 1};
                print_listing(&single, options, false,
                    single.items[0].path, had_error, output_failed);
            }
            return;
        }
        if (progress.completed_entries == 0) {
            /* Long-field preparation or prefix measurement failed before
             * output began; the valid preceding files still need printing. */
            for (i = 0; i < progress.failed_index && !*output_failed; ++i) {
                EntryList single = {remaining.items + i, 1, 1};
                print_listing(&single, options, false,
                    single.items[0].path, had_error, output_failed);
            }
        }
        start += progress.failed_index + 1;
    }
}

static void list_directory(const char *path, const Options *options,
    bool headings, bool *printed_section, bool *had_error, bool *output_failed)
{
    EntryList contents;
    size_t i;

    entry_list_init(&contents);
    /* A failed read may leave valid entries: still sort and print them. */
    if (entry_list_read_directory(&contents, path, options, stderr) != 0)
        *had_error = true;
    entry_list_sort(&contents, options);
    if (*printed_section && fputc('\n', stdout) == EOF)
        *output_failed = true;
    if (headings) {
        char *heading = format_display_name(path, options->name_mode);
        if (heading == NULL)
            report_format_error(path, had_error);
        else {
            if (fprintf(stdout, "%s:\n", heading) < 0)
                *output_failed = true;
            free(heading);
        }
    }
    print_listing(&contents, options, true, path, had_error, output_failed);
    *printed_section = true;

    if (options->recursive) {
        for (i = 0; i < contents.length; ++i) {
            const Entry *child = &contents.items[i];

            /* Child metadata comes from lstat: never follow symlinks, and
             * never revisit the parent/self entries exposed by -a. */
            if (!child->metadata_valid || !S_ISDIR(child->st.st_mode) ||
                strcmp(child->name, ".") == 0 ||
                strcmp(child->name, "..") == 0)
                continue;
            list_directory(child->path, options, headings, printed_section,
                had_error, output_failed);
        }
    }
    /* Keep the parent's owned paths alive throughout all child visits. */
    entry_list_destroy(&contents);
}

int main(int argc, char *argv[])
{
    Options options;
    EntryList operands, files, directories;
    int operand_index, index;
    size_t i;
    bool headings, printed_section = false, output_failed = false;
    bool had_error = false;

    (void)setlocale(LC_ALL, "");
    options_init(&options, isatty(STDOUT_FILENO) != 0, geteuid(),
        getenv("BLOCKSIZE"));
    if (options_parse(&options, argc, argv, &operand_index, stderr) != 0)
        return EXIT_FAILURE;

    headings = options.recursive || argc - operand_index > 1;
    entry_list_init(&operands);
    if (operand_index == argc) {
        if (entry_list_add_path(&operands, ".", ".", 0, true, stderr) != 0)
            had_error = true;
    } else {
        for (index = operand_index; index < argc; ++index) {
            if (entry_list_add_path(&operands, argv[index], argv[index],
                    0, true, stderr) != 0)
                had_error = true;
        }
    }
    if (group_operands(&operands, &options, &files, &directories) != 0) {
        fprintf(stderr, "my_ls: grouping operands: %s\n", strerror(errno));
        entry_list_destroy(&operands);
        return EXIT_FAILURE;
    }
    entry_list_sort(&files, &options);
    entry_list_sort(&directories, &options);
    if (files.length != 0) {
        print_file_operands(&files, &options, &had_error, &output_failed);
        printed_section = true;
    }

    for (i = 0; i < directories.length; ++i) {
        list_directory(directories.items[i].path, &options, headings,
            &printed_section, &had_error, &output_failed);
    }

    entry_list_destroy(&operands);
    if (fflush(stdout) == EOF)
        output_failed = true;
    if (output_failed) {
        fputs("my_ls: standard output: write error\n", stderr);
        had_error = true;
    }
    return had_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
