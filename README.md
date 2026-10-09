# Simplified ls for FreeBSD

`my_ls` is a C implementation of the supplied NetBSD `ls(1)` manual's
short-option interface, intended for a FreeBSD grading environment. It reads
filesystem metadata and directories directly using system calls and the C
library. It does not invoke the system ls or use its output as a test oracle.
The default output is one entry per line, sorted by bytewise pathname order.

```text
./my_ls [-AacdFfhiklnqRrSstuw] [file ...]
```

With no operands, it lists `.`. Options precede operands; `--` ends option
parsing and allows leading-dash names. Only the listed short options are
supported.

## Implemented options

| Option | Behavior |
| --- | --- |
| `-A` | Include hidden names except `.` and `..`; also the superuser default. |
| `-a` | Include all names, including `.` and `..`. |
| `-c` | Select status-change time for `-t` ordering and long output. |
| `-d` | List directory operands themselves; do not dereference link operands. |
| `-F` | Append `/` for directories, `*` for executable regular files, `@` for links, `%` for whiteouts, `=` for sockets, and `\|` for FIFOs. |
| `-f` | Preserve directory enumeration order and operand order within each group. |
| `-h` | Display logical sizes and allocated bytes using binary `B/K/M/G/T/P` units. |
| `-i` | Prefix each entry with its inode number. |
| `-k` | Display allocated blocks in 1024-byte units; logical sizes remain bytes. |
| `-l` | Long output with account names, falling back to numeric IDs if unknown. |
| `-n` | Long output with numeric owner and group IDs. |
| `-q` | Replace non-printable or invalid pathname bytes with `?`. |
| `-R` | Recursively list real child directories, parent before children. |
| `-r` | Reverse the selected sorted order, including name tie-breaks. |
| `-S` | Sort by logical size, largest first. |
| `-s` | Prefix each entry with allocated block usage. |
| `-t` | Sort by the selected timestamp, newest first. |
| `-u` | Select access time for `-t` ordering and long output. |
| `-w` | Preserve the original pathname bytes in output. |

## Precedence and output rules

The rightmost occurrence wins for each pair: `-l`/`-n`, `-c`/`-u`,
`-R`/`-d`, `-h`/`-k`, and `-q`/`-w`. The primary sort selectors `-f`,
`-S`, and `-t` also use the rightmost occurrence. Clustered options and
separate option arguments obey the same rule. For example, `-kh` selects
human units, while `-hk` selects kilobyte block units. `-r` has no effect on
`-f`; unsorted enumeration remains unchanged. `-a` includes `.` and `..`
regardless of `-A`. Equal size/time keys use bytewise names as tie-breaks.

Modification time is the default selected timestamp. Time sorting includes
subsecond precision. Long output shows type/permissions, link count, owner,
group, size (or device major/minor), abbreviated month, day, `HH:MM`, and name.
Symbolic links retain an ` -> target` arrow in long output; `-q` changes the
displayed name without changing the stored path or the target text. Special
permission bits render as `s/S` and `t/T` according to execute permission.

Non-directory operands print before directory contents, with each group
sorted independently. A link operand resolving to a directory is traversed
unless `-d` is active, including with `-l`, `-n`, or `-F`. Links found inside
directories are never followed for recursion. Recursive traversal skips
`.` and `..` even when `-a` displays them. Multiple supplied operands and
recursive listings receive `path:` headings, with blank lines separating
sections. Failed operands still count toward the heading decision.

Native `st_blocks` uses 512-byte units. `-s` rounds each entry's allocated
bytes up to the display unit. Without `-h` or `-k`, a positive `BLOCKSIZE`
byte value is accepted, with optional case-insensitive `K/M/G/T/P` binary
suffix; missing, invalid, zero, or overflowing values use 512 bytes. `-k`
overrides `BLOCKSIZE`; `-h` uses allocated bytes directly. Directory totals
sum raw allocated blocks before a single conversion. Long directory listings
always show `total`, even when empty; short `-s` listings show it only on a
terminal. Operand lists never receive totals.

Terminal stdout defaults to `-q`; redirected stdout defaults to `-w`.
Printability follows the active locale, including valid printable multibyte
characters. Raw output may contain control bytes, so use `-q` for inspection.

## FreeBSD prerequisites and commands

Use a FreeBSD system with its base C compiler (`cc`), BSD `make`, `/bin/sh`,
and standard utilities. The tests use FreeBSD `stat -f`, `touch -h`,
`truncate`, `mkfifo`, `mktemp`, `id`, `cmp`, `diff`, `od`, `tr`, `grep`,
`find`, `sleep`, and ordinary file/permission utilities. No third-party
libraries are required. Git is optional for inspecting the local handoff.
Run as a normal user to exercise unreadable-directory checks; root skips
those fixtures because it bypasses their permission restriction. A UTF-8
locale enables additional formatter cases, which explicitly skip if absent.

From the project root:

```sh
make
./my_ls
./my_ls -AilF .
./my_ls -nhstr README.md Makefile
./my_ls -R src tests
./my_ls -dF include
./my_ls -q -- ./-leading-dash-name
BLOCKSIZE=1K ./my_ls -s .
make test
make clean
```

Compilation defaults to `cc -std=c11 -Wall -Wextra -Wpedantic -Werror`.
Override standard make variables on the command line if needed, for example
`make CC=clang`; retain the warning flags for delivery. `make unit-test`
runs just the four C unit executables. `make test` builds the program,
runs those units, and runs the full CLI acceptance matrix.

The fresh delivery gate on FreeBSD is:

```sh
make clean && make && make test && make clean && git status --short --ignored
```

## Module layout and tests

| Path | Responsibility |
| --- | --- |
| `src/main.c` | Program lifetime, operand groups, sections, recursion, final status. |
| `include/options.h`, `src/options.c` | Ordered option parsing, defaults, `BLOCKSIZE`. |
| `include/entry.h`, `src/entry.c` | Owned paths/metadata, directory collection, link targets, cleanup. |
| `include/sort.h`, `src/sort.c` | Name, size, selected time, reverse and unsorted ordering. |
| `include/format.h`, `src/format.c` | Name display, suffixes, permissions, fields, alignment and totals. |
| `tests/test_options.c` | Parser defaults, ordered pairs and block-size validation. |
| `tests/test_entry.c` | Real collector fixtures and allocation-failure ownership checks. |
| `tests/test_sort.c` | Comparator keys, ties, time precision and reverse ordering. |
| `tests/test_format.c` | Fields, permission/type modes, locale, block arithmetic and output failures. |
| `tests/test_ls.sh` | CLI fixtures, every option, combinations, precedence, errors and clean rebuild. |

The CLI tests use isolated temporary directories, `LC_ALL=C`, `TZ=UTC`, and
an initial `BLOCKSIZE=512`. Literal expectations and fixture metadata from
`stat` provide independent assertions. Unsorted operand tests avoid depending
on filesystem enumeration order; ctime ordering uses separated metadata
changes because ctime cannot be assigned directly. Exit cleanup restores the
unreadable fixture's permissions before removing its temporary directory.
The clean-rebuild case builds a private source copy, runs its unit tests and
checks rebuilt CLI output, then checks that cleanup removed its artifacts.

## Errors and platform limits

Success returns status zero. Any operand, directory-read, allocation,
formatting, or stdout-write failure produces nonzero status. Diagnostics go
to stderr with the program and affected path; valid remaining operands and
recursive siblings continue where possible. Unknown options print an illegal
option diagnostic and usage, with no listing. Paths and link targets use
dynamic storage rather than fixed `PATH_MAX` buffers.

FIFO, regular file, directory, executable and symlink fixtures run through
the CLI. Socket mode/suffix behavior is covered with synthetic formatter
metadata. Character/block device formatting is tested synthetically; the CLI
also checks `/dev/null` when available. Creating devices or whiteouts can
require privileges and filesystem support, so no privileged creation is
part of the test gate. FreeBSD whiteout collection and rendering are guarded
by available `DT_WHT`/`S_IFWHT` constants. The collector uses FreeBSD's
`__opendir2(path, DTF_NODUP)` to expose whiteouts that ordinary `opendir`
hides; its declaration and flags are in the official
[FreeBSD dirent.h](https://github.com/freebsd/freebsd-src/blob/main/include/dirent.h).
An injected libc enumeration fixture exercises collection through this open
path and real `lstat` fallback, without creating a privileged whiteout.
CLI byte fixtures cover multiple-directory and recursive headings under
`-q` and `-w`. A dedicated command harness checks the terminal heading default
and injected formatter allocation failures, including affected-path diagnostics
and continued directory output. Stream failures remain standard-output errors.
The manual's archive-state type markers have no fixture or implementation
branch here; ordinary FreeBSD headers do not provide their mode constants.
They remain a platform-specific limitation if such headers are introduced.

The authoring host is Windows and has no available C compiler, `make`, or
POSIX shell. Source and test coverage were reviewed statically there; no
FreeBSD build, shell syntax check, runtime red/green cycle, or passing test
gate is claimed. The commands above must be run on FreeBSD before claiming
runtime acceptance or warning-free compilation.

## Local delivery

GitHub publication is not part of this local handoff. The deliverables are
the source, headers, Makefile, tests, README and ignore rules. Generated
`my_ls`, object files, four test executables, cores, `tmp/`, and local agent
scratch `.superpowers/` are ignored and are not submission artifacts.
`make clean` removes the program, source/test objects and test executables.
