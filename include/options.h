#ifndef MY_LS_OPTIONS_H
#define MY_LS_OPTIONS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

typedef enum { SORT_NAME, SORT_NONE, SORT_SIZE, SORT_TIME } SortMode;
typedef enum { TIME_MTIME, TIME_CTIME, TIME_ATIME } TimeMode;
typedef enum { NAME_QMARK, NAME_RAW } NameMode;
typedef enum { SIZE_DEFAULT, SIZE_KIB, SIZE_HUMAN } SizeMode;

typedef struct {
    bool show_all;
    bool almost_all;
    bool directory_as_file;
    bool classify;
    bool show_inode;
    bool show_blocks;
    bool recursive;
    bool reverse;
    bool long_format;
    bool numeric_ids;
    bool stdout_is_tty;
    SortMode sort_mode;
    TimeMode time_mode;
    NameMode name_mode;
    SizeMode size_mode;
    uint64_t block_size;
} Options;

void options_init(Options *, bool stdout_is_tty, uid_t euid,
    const char *blocksize_env);
int options_parse(Options *, int argc, char *const argv[],
    int *operand_index, FILE *err);
const char *options_usage(void);

#endif
