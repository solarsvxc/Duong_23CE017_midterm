#include "options.h"

/* Parse positive decimal bytes, with an optional binary-unit suffix. */
static uint64_t parse_blocksize(const char *input)
{
    const uint64_t fallback = 512;
    uint64_t value = 0;
    unsigned int exponent = 0;
    unsigned int i;

    if (input == NULL || *input < '0' || *input > '9')
        return fallback;
    while (*input >= '0' && *input <= '9') {
        unsigned int digit = (unsigned int)(*input - '0');
        if (value > (UINT64_MAX - digit) / 10)
            return fallback;
        value = value * 10 + digit;
        ++input;
    }
    if (*input != '\0') {
        switch (*input) {
        case 'K': case 'k': exponent = 1; break;
        case 'M': case 'm': exponent = 2; break;
        case 'G': case 'g': exponent = 3; break;
        case 'T': case 't': exponent = 4; break;
        case 'P': case 'p': exponent = 5; break;
        default: return fallback;
        }
        ++input;
        if (*input != '\0')
            return fallback;
    }
    if (value == 0)
        return fallback;
    for (i = 0; i < exponent; ++i) {
        if (value > UINT64_MAX / 1024)
            return fallback;
        value *= 1024;
    }
    return value;
}

void options_init(Options *options, bool stdout_is_tty, uid_t euid,
    const char *blocksize_env)
{
    *options = (Options){0};
    options->almost_all = euid == 0;
    options->stdout_is_tty = stdout_is_tty;
    options->sort_mode = SORT_NAME;
    options->time_mode = TIME_MTIME;
    options->name_mode = stdout_is_tty ? NAME_QMARK : NAME_RAW;
    options->size_mode = SIZE_DEFAULT;
    options->block_size = parse_blocksize(blocksize_env);
}

const char *options_usage(void)
{
    return "usage: my_ls [-AacdFfhiklnqRrSstuw] [file ...]\n";
}

int options_parse(Options *options, int argc, char *const argv[],
    int *operand_index, FILE *err)
{
    int index;

    for (index = 1; index < argc; ++index) {
        const char *letter = argv[index];
        if (letter[0] != '-' || letter[1] == '\0')
            break;
        if (letter[1] == '-' && letter[2] == '\0') {
            ++index;
            break;
        }
        for (++letter; *letter != '\0'; ++letter) {
            switch (*letter) {
            case 'A': options->almost_all = true; break;
            case 'a': options->show_all = true; break;
            case 'c': options->time_mode = TIME_CTIME; break;
            case 'd':
                options->directory_as_file = true;
                options->recursive = false;
                break;
            case 'F': options->classify = true; break;
            case 'f': options->sort_mode = SORT_NONE; break;
            case 'h': options->size_mode = SIZE_HUMAN; break;
            case 'i': options->show_inode = true; break;
            case 'k': options->size_mode = SIZE_KIB; break;
            case 'l':
                options->long_format = true;
                options->numeric_ids = false;
                break;
            case 'n':
                options->long_format = true;
                options->numeric_ids = true;
                break;
            case 'q': options->name_mode = NAME_QMARK; break;
            case 'R':
                options->recursive = true;
                options->directory_as_file = false;
                break;
            case 'r': options->reverse = true; break;
            case 'S': options->sort_mode = SORT_SIZE; break;
            case 's': options->show_blocks = true; break;
            case 't': options->sort_mode = SORT_TIME; break;
            case 'u': options->time_mode = TIME_ATIME; break;
            case 'w': options->name_mode = NAME_RAW; break;
            default:
                *operand_index = index;
                fprintf(err, "my_ls: illegal option -- %c\n", *letter);
                fputs(options_usage(), err);
                return -1;
            }
        }
    }
    *operand_index = index;
    return 0;
}
