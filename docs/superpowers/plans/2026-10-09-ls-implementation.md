# Simplified `ls(1)` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a robust, modular `my_ls` in C implementing every option and behavior in the supplied `ls(1)` subset on FreeBSD.

**Architecture:** Parse ordered short options into one `Options` value, collect owned `Entry` records from operands and directories, sort those records independently of collection, and render them through a dedicated formatter. `main.c` coordinates operand grouping and depth-first directory traversal while every lower-level module exposes a narrow testable interface.

**Tech Stack:** FreeBSD system `cc`, C11, POSIX/FreeBSD filesystem APIs, portable `make`, and `/bin/sh` integration tests.

**Spec:** `docs/superpowers/specs/2026-10-09-ls-design.md`

## Global Constraints

- Target FreeBSD and compile with `cc -std=c11 -Wall -Wextra -Wpedantic -Werror`.
- Produce `./my_ls [-AacdFfhiklnqRrSstuw] [file ...]`; do not add options outside this synopsis.
- Never execute `/bin/ls`, parse its output, or copy GNU/BSD `ls` source code.
- Use dynamic allocation for paths and symlink targets; do not use a fixed `PATH_MAX` buffer.
- Default output is one entry per line; diagnostics go to standard error and any error makes the final status nonzero without stopping valid operands.
- Keep implementation in focused `.c`/`.h` modules and keep all functions warning-clean under the required flags.
- Deliver `Makefile`, English `README.md`, tests, and `.gitignore`; do not create or publish a GitHub repository.
- Run every production-code change through a witnessed RED, GREEN, REFACTOR cycle.

## Review Focus

- An operand named with a leading `-` must be addressable after `--`; covered by Task 4's `operand_after_double_dash` test.
- A long nested path near the filesystem limit must be joined dynamically without truncation; covered by Task 2's `long_nested_path` test.
- A sparse file larger than 4 GiB must print without integer overflow; covered by Task 7's `large_sparse_file` test.
- A symlink cycle must never cause recursive traversal; covered by Task 8's `symlink_cycle_not_followed` test.
- Invalid, zero, overflowing, and suffixed `BLOCKSIZE` values must fall back or convert deterministically; covered by Task 1's `blocksize_table` test.

---

### Task 1: Build Harness and Ordered Option Parsing

**Files:**
- Create: `Makefile`
- Create: `include/options.h`
- Create: `src/options.c`
- Create: `tests/test_options.c`

**Interfaces:**
- Consumes: C runtime, `isatty(3)` result supplied by the caller, effective UID, and the `BLOCKSIZE` environment string.
- Produces: enums `SortMode { SORT_NAME, SORT_NONE, SORT_SIZE, SORT_TIME }`, `TimeMode { TIME_MTIME, TIME_CTIME, TIME_ATIME }`, `NameMode { NAME_QMARK, NAME_RAW }`, and `SizeMode { SIZE_DEFAULT, SIZE_KIB, SIZE_HUMAN }`; `Options { bool show_all; bool almost_all; bool directory_as_file; bool classify; bool show_inode; bool show_blocks; bool recursive; bool reverse; bool long_format; bool numeric_ids; bool stdout_is_tty; SortMode sort_mode; TimeMode time_mode; NameMode name_mode; SizeMode size_mode; uint64_t block_size; }`; `void options_init(Options *, bool stdout_is_tty, uid_t euid, const char *blocksize_env)`; `int options_parse(Options *, int argc, char *const argv[], int *operand_index, FILE *err)`; `const char *options_usage(void)`.

- [ ] **Step 1: Write the failing option tests**

Add table-driven tests named `defaults`, `all_supported_flags`, `rightmost_pairs`, `sort_selector_rightmost`, `root_enables_almost_all`, `double_dash_stops_options`, `unknown_option_reports_usage`, and `blocksize_table`. Assert exact enum/boolean fields and these block sizes: unset/invalid/`0`/overflow -> `512`, `2K` -> `2048`, and `1M` -> `1048576`.

- [ ] **Step 2: Run the option test and verify RED**

Run: `make tests/test_options`

Expected: compilation fails because `include/options.h` and its interfaces do not exist.

- [ ] **Step 3: Implement the option model and parser**

Define `Options` with hidden-entry flags, display flags, `long_format`, `numeric_ids`, the four enums above, `uint64_t block_size`, and `stdout_is_tty`. Parse clustered leading short options and `--` without global `getopt` state. Apply rightmost-wins while scanning, make `-n` and `-l` both enable long format, and reject only unsupported option letters.

- [ ] **Step 4: Run the option test and verify GREEN**

Run: `make tests/test_options && ./tests/test_options`

Expected: exit `0` and a summary reporting every option test passed.

- [ ] **Step 5: Commit the task locally**

```sh
git add Makefile include/options.h src/options.c tests/test_options.c
git commit -m "feat: parse ls options in command order"
```

### Task 2: Owned Entry Model and Directory Collection

**Files:**
- Create: `include/entry.h`
- Create: `src/entry.c`
- Create: `tests/test_entry.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `Options` hidden-entry policy and FreeBSD `lstat(2)`, `stat(2)`, `readlink(2)`, `opendir(3)`, and `readdir(3)`.
- Produces: `Entry { char *name; char *path; char *link_target; struct stat st; unsigned char dirent_type; bool metadata_valid; bool is_operand; }`; `EntryList { Entry *items; size_t length; size_t capacity; }`; `void entry_list_init(EntryList *)`; `int entry_list_add_path(EntryList *, const char *name, const char *path, unsigned char dirent_type, bool is_operand, FILE *err)`; `int entry_list_read_directory(EntryList *, const char *path, const Options *, FILE *err)`; `bool entry_operand_is_directory(const Entry *, bool no_dereference)`; `void entry_list_destroy(EntryList *)`.

- [ ] **Step 1: Write the failing entry tests**

Add tests `metadata_and_symlink_target`, `default_hides_dot_names`, `almost_all_excludes_dot_entries`, `all_includes_dot_entries`, `missing_path_returns_error`, `conditional_whiteout_without_lstat`, `partial_failure_cleans_up`, and `long_nested_path`. The long-path test builds nested components, asserts the final `Entry.path` equals the complete input byte-for-byte, and destroys the list under an allocation/leak checker when available.

- [ ] **Step 2: Run the entry test and verify RED**

Run: `make tests/test_entry`

Expected: compilation fails because `include/entry.h` is missing.

- [ ] **Step 3: Implement entry ownership and collection**

Use checked geometric growth for `EntryList`, allocate joins as `parent + optional slash + name + NUL`, retain `lstat` metadata for rendering, call `stat` only inside `entry_operand_is_directory`, and read a symlink target with a growable buffer. Pass `DT_UNKNOWN` for operands and the actual `dirent.d_type` for directory entries; when FreeBSD reports `DT_WHT` and `lstat` cannot supply metadata, retain a conditional whiteout entry with synthetic `S_IFWHT` mode and `metadata_valid == false`. Preserve `readdir` order and skip names according to `Options` before metadata collection.

- [ ] **Step 4: Run entry and option tests and verify GREEN**

Run: `make tests/test_entry tests/test_options && ./tests/test_entry && ./tests/test_options`

Expected: both binaries exit `0`; missing paths produce the asserted diagnostic without a crash.

- [ ] **Step 5: Commit the task locally**

```sh
git add Makefile include/entry.h src/entry.c tests/test_entry.c
git commit -m "feat: collect owned filesystem entries"
```

### Task 3: Deterministic Sorting

**Files:**
- Create: `include/sort.h`
- Create: `src/sort.c`
- Create: `tests/test_sort.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `EntryList`, `Options.sort_mode`, `Options.time_mode`, and `Options.reverse`.
- Produces: `void entry_list_sort(EntryList *, const Options *)`.

- [ ] **Step 1: Write the failing sorting tests**

Construct synthetic `Entry` values and add `name_order`, `size_descending`, `mtime_descending`, `ctime_and_atime_selection`, `subsecond_time_order`, `equal_key_name_tiebreak`, `reverse_reverses_tiebreak`, and `none_preserves_input_order`. Assert the complete ordered name array for every case.

- [ ] **Step 2: Run the sorting test and verify RED**

Run: `make tests/test_sort`

Expected: compilation fails because `entry_list_sort` is undefined.

- [ ] **Step 3: Implement sorting**

Use `qsort(3)` with one active comparator context established by `entry_list_sort`; compare integer values without subtraction, compare FreeBSD `timespec` seconds then nanoseconds, use bytewise `strcmp` as the tie-break, and bypass `qsort` for `SORT_NONE`.

- [ ] **Step 4: Run all unit tests and verify GREEN**

Run: `make unit-test`

Expected: option, entry, and sorting suites all exit `0`.

- [ ] **Step 5: Commit the task locally**

```sh
git add Makefile include/sort.h src/sort.c tests/test_sort.c
git commit -m "feat: sort entries by name size and time"
```

### Task 4: Basic CLI, Operand Grouping, and Error Continuation

**Files:**
- Create: `src/main.c`
- Create: `include/format.h`
- Create: `src/format.c`
- Create: `tests/test_ls.sh`
- Modify: `Makefile`

**Interfaces:**
- Consumes: option parsing, `EntryList`, and `entry_list_sort`.
- Produces: executable `my_ls`; `int format_listing(FILE *out, const EntryList *, const Options *, bool directory_contents)` initially supporting plain names; process exit `0` on success and `>0` after any error.

- [ ] **Step 1: Write failing basic CLI scenarios**

In `tests/test_ls.sh`, isolate each case in a `mktemp -d` fixture and add `no_operands`, `single_file_operand`, `mixed_files_before_directory`, `multiple_operand_heading`, `empty_directory`, `hidden_modes`, `operand_after_double_dash`, `unknown_option`, and `missing_operand_continues`. Assert exact stdout, required stderr path text, and exit status; for the leading-dash case, enter the fixture and invoke the absolute test binary as `"$binary" -- -sample` so the operand itself begins with `-`.

- [ ] **Step 2: Run the CLI test and verify RED**

Run: `make my_ls && sh tests/test_ls.sh ./my_ls`

Expected: build fails because `src/main.c` and `format_listing` do not exist.

- [ ] **Step 3: Implement the basic command flow**

Initialize locale and options, default operands to `.`, collect valid operands while retaining an error flag, split them into non-directory and directory lists, sort each list, print files first, and list each directory. Print headings when operand count exceeds one and separate sections with one blank line. Do not add recursion or advanced formatting yet.

- [ ] **Step 4: Run the complete current suite and verify GREEN**

Run: `make test`

Expected: all unit suites and basic CLI scenarios pass with no compiler warnings.

- [ ] **Step 5: Commit the task locally**

```sh
git add Makefile include/format.h src/format.c src/main.c tests/test_ls.sh
git commit -m "feat: list files and directory contents"
```

### Task 5: Display-Name Policy and `-F` Classification

**Files:**
- Create: `tests/test_format.c`
- Modify: `include/format.h`
- Modify: `src/format.c`
- Modify: `tests/test_ls.sh`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `Entry` mode/type, `Options.name_mode`, and `Options.classify`.
- Produces: `char *format_display_name(const char *name, NameMode mode)` returning owned memory; `char format_type_suffix(const Entry *)` returning `\0` when no suffix applies.

- [ ] **Step 1: Write failing name and suffix tests**

Add pure cases for directory `/`, symlink `@`, whiteout `%` when `S_IFWHT` exists, socket `=`, FIFO `|`, executable regular file `*`, and non-executable regular file `\0`. Add CLI cases `classify_fixture`, `q_replaces_control_byte`, `w_preserves_control_byte`, `terminal_default_is_unit_tested`, and `q_w_rightmost`; compare control-byte output through `od -An -tx1` under `LC_ALL=C`.

- [ ] **Step 2: Run focused tests and verify RED**

Run: `make tests/test_format my_ls && ./tests/test_format && sh tests/test_ls.sh ./my_ls`

Expected: formatter test fails because display-name and suffix interfaces are missing.

- [ ] **Step 3: Implement display names and classification**

Use `mbrtowc`/`iswprint` with a bytewise fallback that always advances on invalid sequences. Apply the exact suffix precedence from the spec and append suffixes only to displayed names, never stored paths.

- [ ] **Step 4: Run the complete suite and verify GREEN**

Run: `make test`

Expected: all suites pass, including raw-byte assertions, with no warnings.

- [ ] **Step 5: Commit the task locally**

```sh
git add Makefile include/format.h src/format.c tests/test_format.c tests/test_ls.sh
git commit -m "feat: render safe names and file suffixes"
```

### Task 6: Long and Numeric Formats

**Files:**
- Modify: `include/format.h`
- Modify: `src/format.c`
- Modify: `tests/test_format.c`
- Modify: `tests/test_ls.sh`

**Interfaces:**
- Consumes: `Entry.st`, selected `TimeMode`, owner/group databases, and symlink targets.
- Produces: `void format_mode(mode_t mode, unsigned char dirent_type, char out[11])`; extended `format_listing` for aligned `-l`/`-n` output.

- [ ] **Step 1: Write failing permission and long-format tests**

Add table cases for regular/directory/link/block/character/socket/FIFO/conditional-whiteout type characters; permissions `0644`, `0755`, setuid/setgid executable and non-executable forms, plus sticky `t/T`. Add CLI cases `numeric_long_fields`, `named_long_fields`, `long_symlink_arrow`, `long_timestamp_mtime`, `c_u_rightmost`, and `l_n_rightmost`, setting `LC_ALL=C` and `TZ=UTC` and asserting `Jan  2 03:04` for a controlled timestamp.

- [ ] **Step 2: Run focused tests and verify RED**

Run: `make tests/test_format my_ls && ./tests/test_format && sh tests/test_ls.sh ./my_ls`

Expected: failures show missing mode text and absent long-format columns.

- [ ] **Step 3: Implement long-format rendering**

Render all ten mode characters directly, resolve names with `getpwuid`/`getgrgid` and numeric fallback, use numeric IDs for `-n`, use `major()`/`minor()` for device files, format the selected timestamp, measure all column widths before output, and append the original symlink target after ` -> `.

- [ ] **Step 4: Run the complete suite and verify GREEN**

Run: `make test`

Expected: all long and short formats pass without locale-dependent failures.

- [ ] **Step 5: Commit the task locally**

```sh
git add include/format.h src/format.c tests/test_format.c tests/test_ls.sh
git commit -m "feat: add long and numeric listing formats"
```

### Task 7: Inodes, Allocated Blocks, Human Sizes, and Totals

**Files:**
- Modify: `include/format.h`
- Modify: `src/format.c`
- Modify: `tests/test_format.c`
- Modify: `tests/test_ls.sh`

**Interfaces:**
- Consumes: `st_ino`, `st_blocks`, `st_size`, configured block size, output-terminal state, and directory-content context.
- Produces: `uintmax_t format_block_units(blkcnt_t blocks_512, uint64_t unit_bytes)`; `int format_human_size(uintmax_t bytes, char *buffer, size_t size)`; `format_listing` support for `-i`, `-s`, `-h`, `-k`, `BLOCKSIZE`, devices, and `total`.

- [ ] **Step 1: Write failing numeric-format tests**

Assert block rounding for 0, 1, 2, and very large `st_blocks`; human strings `0B`, `1023B`, `1.0K`, `1.5K`, and `1.0M`; and buffer-too-small failure. Add CLI cases `inode_prefix`, `blocksize_512_and_1k`, `h_overrides_k`, `k_does_not_change_long_size`, `long_total_even_empty`, `s_total_only_for_tty_via_formatter`, `device_major_minor_when_available`, and `large_sparse_file`, where the sparse file is 5 GiB and its exact byte size appears under `-n`.

- [ ] **Step 2: Run focused tests and verify RED**

Run: `make tests/test_format my_ls && ./tests/test_format && sh tests/test_ls.sh ./my_ls`

Expected: failures show missing prefixes, unit conversion, and totals.

- [ ] **Step 3: Implement safe size conversion and totals**

Avoid `blocks * 512` overflow by quotient/remainder conversion, cap only when the mathematical result exceeds `uintmax_t`, use binary units through `P`, sum raw blocks before total conversion, and include inode/block fields in width measurement.

- [ ] **Step 4: Run the complete suite and verify GREEN**

Run: `make test`

Expected: all size cases pass on FreeBSD without signed/unsigned warnings.

- [ ] **Step 5: Commit the task locally**

```sh
git add include/format.h src/format.c tests/test_format.c tests/test_ls.sh
git commit -m "feat: report inodes blocks sizes and totals"
```

### Task 8: `-d`, Recursive Traversal, and Symlink Boundaries

**Files:**
- Modify: `src/main.c`
- Modify: `tests/test_ls.sh`

**Interfaces:**
- Consumes: fully formatted/sorted directory `EntryList` values and `entry_operand_is_directory`.
- Produces: depth-first `-R` traversal with headings and accumulated status; rightmost `-R`/`-d` behavior.

- [ ] **Step 1: Write failing traversal tests**

Add `d_lists_directory_operand`, `symlink_directory_default`, `d_does_not_dereference_symlink`, `recursive_depth_first_sections`, `recursive_never_visits_dot_entries`, `recursive_does_not_follow_child_symlink`, `symlink_cycle_not_followed`, `R_d_rightmost`, and `unreadable_child_continues_when_not_root`. Assert exact heading order and that every real directory appears once.

- [ ] **Step 2: Run traversal tests and verify RED**

Run: `make my_ls && sh tests/test_ls.sh ./my_ls`

Expected: recursive and `-d` scenarios fail while earlier scenarios remain green.

- [ ] **Step 3: Implement traversal in `main.c`**

Print each directory before recursing through its already sorted child list, recurse only when `lstat` says the child itself is a directory, never recurse into `.` or `..`, and carry a shared `had_error` flag. Treat a command-line symlink-to-directory as a directory only when `-d` is inactive.

- [ ] **Step 4: Run the complete suite and verify GREEN**

Run: `make test`

Expected: all traversal, formatting, sorting, and option tests pass.

- [ ] **Step 5: Commit the task locally**

```sh
git add src/main.c tests/test_ls.sh
git commit -m "feat: add directory and recursive traversal modes"
```

### Task 9: Full Manual Coverage, Report, and Delivery Hygiene

**Files:**
- Create: `README.md`
- Create: `.gitignore`
- Modify: `tests/test_ls.sh`
- Modify: `Makefile`
- Modify: source files only when a new failing acceptance test exposes a gap

**Interfaces:**
- Consumes: every public command behavior produced by Tasks 1-8.
- Produces: documented submission and one `make test` acceptance gate covering every manual option.

- [ ] **Step 1: Add the failing acceptance matrix**

Add `every_option_is_accepted`, `combined_AilF`, `combined_nhstr`, `sort_cli_name_size_time_reverse_none`, `time_cli_mtime_ctime_atime`, `all_rightmost_pairs_cli`, `error_exit_status`, and `clean_rebuild`. Each case asserts behavior, not merely exit success; the matrix must account for every letter in `AacdFfhiklnqRrSstuw`.

- [ ] **Step 2: Run the acceptance suite and verify RED or identify already-green cases**

Run: `make clean && make test`

Expected: any uncovered manual behavior fails for the feature-specific reason. For a case already green, temporarily invert its key assertion once to witness failure, restore it, and rerun before proceeding.

- [ ] **Step 3: Close only acceptance gaps and write the report files**

Fix production code only for witnessed failures. Write `README.md` with overview, implemented options, precedence, module layout, FreeBSD prerequisites, `make`/run/test/clean commands, examples, error behavior, platform-limited whiteout/device/archive tests, and the explicit statement that GitHub publication is not part of this local handoff. Add `my_ls`, `*.o`, `tests/*.o`, `tests/test_options`, `tests/test_entry`, `tests/test_sort`, `tests/test_format`, `core`, `core.*`, and `tmp/` to `.gitignore`.

- [ ] **Step 4: Run fresh delivery verification**

Run: `make clean && make && make test && make clean && git status --short --ignored`

Expected: build and all tests exit `0`; compilation emits no warnings; final `make clean` leaves no binary/object files; only intended source, documentation, test, and ignored `tmp/` paths remain.

- [ ] **Step 5: Commit the task locally if repository permissions allow**

```sh
git add .gitignore Makefile README.md include src tests
git commit -m "docs: complete ls project report and verification"
```

If `.git` remains read-only in the execution environment, record the successful verification and the exact commit command instead of treating the unavailable commit as a product failure.
