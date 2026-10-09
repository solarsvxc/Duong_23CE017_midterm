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
            entry_operand_is_directory(&owner->items[i],
                options->long_format || options->classify);
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

int main(int argc, char *argv[])
{
    Options options;
    EntryList operands, files, directories;
    int operand_index, index, status = 0;
    size_t i;
    bool headings, printed_section = false, output_failed = false;

    (void)setlocale(LC_ALL, "");
    options_init(&options, isatty(STDOUT_FILENO) != 0, geteuid(),
        getenv("BLOCKSIZE"));
    if (options_parse(&options, argc, argv, &operand_index, stderr) != 0)
        return EXIT_FAILURE;

    headings = argc - operand_index > 1;
    entry_list_init(&operands);
    if (operand_index == argc) {
        if (entry_list_add_path(&operands, ".", ".", 0, true, stderr) != 0)
            status = EXIT_FAILURE;
    } else {
        for (index = operand_index; index < argc; ++index) {
            if (entry_list_add_path(&operands, argv[index], argv[index],
                    0, true, stderr) != 0)
                status = EXIT_FAILURE;
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
        if (format_listing(stdout, &files, &options, false) != 0)
            output_failed = true;
        printed_section = true;
    }

    for (i = 0; i < directories.length; ++i) {
        EntryList contents;
        const char *path = directories.items[i].path;

        entry_list_init(&contents);
        /* A failed read may leave valid entries: still sort and print them. */
        if (entry_list_read_directory(&contents, path, &options, stderr) != 0)
            status = EXIT_FAILURE;
        entry_list_sort(&contents, &options);
        if (printed_section && fputc('\n', stdout) == EOF)
            output_failed = true;
        if (headings && fprintf(stdout, "%s:\n", path) < 0)
            output_failed = true;
        if (format_listing(stdout, &contents, &options, true) != 0)
            output_failed = true;
        printed_section = true;
        entry_list_destroy(&contents);
    }

    entry_list_destroy(&operands);
    if (fflush(stdout) == EOF)
        output_failed = true;
    if (output_failed) {
        fputs("my_ls: standard output: write error\n", stderr);
        status = EXIT_FAILURE;
    }
    return status;
}
