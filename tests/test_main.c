#include "entry.h"
#include "format.h"
#include "options.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Fail only formatter allocations; collection, traversal, and the command's
 * error handling remain real. One failure allows later sections to continue. */
static size_t format_fail_after = SIZE_MAX;

static bool fail_format_allocation(void)
{
    if (format_fail_after == SIZE_MAX)
        return false;
    if (format_fail_after-- != 0)
        return false;
    format_fail_after = SIZE_MAX;
    errno = ENOMEM;
    return true;
}

static void *format_malloc(size_t size)
{
    return fail_format_allocation() ? NULL : malloc(size);
}

static void *format_calloc(size_t count, size_t size)
{
    return fail_format_allocation() ? NULL : calloc(count, size);
}

#define malloc format_malloc
#define calloc format_calloc
#include "../src/format.c"
#undef calloc
#undef malloc

#define main my_ls_main
#include "../src/main.c"
#undef main

static void read_capture(FILE *file, char *buffer, size_t size)
{
    size_t length;
    rewind(file);
    length = fread(buffer, 1, size - 1, file);
    assert(!ferror(file));
    buffer[length] = '\0';
}

/* Mislabeling an allocation failure as a write error, losing errno/path,
 * naming the first entry for a later failure, or abandoning valid directories
 * fails these assertions on the actual command's stderr/status/stdout. */
static void formatter_failure(size_t fail_after, bool long_format,
    bool directory, const char *affected_path)
{
    char *files[] = {"my_ls", long_format ? "-n" : "-q",
        "first", "second", "later", NULL};
    char *directories[] = {"my_ls", "-qRf", "tree", "later", NULL};
    FILE *out = tmpfile(), *err = tmpfile();
    int saved_out = dup(STDOUT_FILENO), saved_err = dup(STDERR_FILENO);
    int status;
    char output[4096], diagnostic[4096];
    assert(out != NULL && err != NULL);
    assert(saved_out >= 0 && saved_err >= 0);
    assert(fflush(stdout) == 0 && fflush(stderr) == 0);
    assert(dup2(fileno(out), STDOUT_FILENO) >= 0);
    assert(dup2(fileno(err), STDERR_FILENO) >= 0);
    format_fail_after = fail_after;
    status = my_ls_main(directory ? 4 : 5,
        directory ? directories : files);
    format_fail_after = SIZE_MAX;
    assert(fflush(stdout) == 0 && fflush(stderr) == 0);
    assert(dup2(saved_out, STDOUT_FILENO) >= 0);
    assert(dup2(saved_err, STDERR_FILENO) >= 0);
    assert(close(saved_out) == 0 && close(saved_err) == 0);
    read_capture(out, output, sizeof(output));
    read_capture(err, diagnostic, sizeof(diagnostic));
    assert(status == EXIT_FAILURE);
    assert(strstr(diagnostic, affected_path) != NULL);
    assert(strstr(diagnostic, strerror(ENOMEM)) != NULL);
    assert(strstr(diagnostic, "format") != NULL);
    assert(strstr(diagnostic, "standard output") == NULL);
    assert(strstr(output, "later:\n") != NULL);
    assert(fclose(out) == 0 && fclose(err) == 0);
}

/* Terminal defaults must sanitize headings just as explicit -q does. */
static void terminal_heading_default(void)
{
    Options options;
    FILE *out = tmpfile();
    int saved_out = dup(STDOUT_FILENO);
    bool printed = false, had_error = false, output_failed = false;
    char output[64];
    assert(out != NULL && saved_out >= 0);
    options_init(&options, true, (uid_t)1000, NULL);
    assert(fflush(stdout) == 0);
    assert(dup2(fileno(out), STDOUT_FILENO) >= 0);
    list_directory("tty\001\n", &options, true, &printed, &had_error,
        &output_failed);
    assert(fflush(stdout) == 0);
    assert(dup2(saved_out, STDOUT_FILENO) >= 0);
    assert(close(saved_out) == 0);
    read_capture(out, output, sizeof(output));
    assert(strcmp(output, "tty??:\n") == 0);
    assert(!had_error && !output_failed);
    assert(fclose(out) == 0);
}

int main(void)
{
    char fixture[] = "/tmp/my_ls_main.XXXXXX";
    int original_directory = open(".", O_RDONLY);
    FILE *file;
    assert(original_directory >= 0 && mkdtemp(fixture) != NULL);
    assert(chdir(fixture) == 0);
    file = fopen("first", "w");
    assert(file != NULL && fclose(file) == 0);
    file = fopen("second", "w");
    assert(file != NULL && fclose(file) == 0);
    assert(mkdir("later", 0700) == 0 && mkdir("tree", 0700) == 0);
    file = fopen("tree/child", "w");
    assert(file != NULL && fclose(file) == 0);
    assert(mkdir("tty\001\n", 0700) == 0);
    formatter_failure(1, false, false, "second:");
    formatter_failure(0, true, false, "first:");
    formatter_failure(3, true, false, "second:");
    /* -f retains tree before later; valid output follows the failed section. */
    formatter_failure(0, false, true, "tree:");
    formatter_failure(1, false, true, "tree/child:");
    terminal_heading_default();
    assert(unlink("first") == 0 && unlink("second") == 0);
    assert(unlink("tree/child") == 0);
    assert(rmdir("tree") == 0 && rmdir("later") == 0);
    assert(rmdir("tty\001\n") == 0);
    assert(fchdir(original_directory) == 0);
    assert(close(original_directory) == 0 && rmdir(fixture) == 0);
    puts("All 6 main regression tests passed.");
    return 0;
}
