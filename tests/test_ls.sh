#!/bin/sh
# Each case uses real operands and exact, independently derived output.
set -eu
LC_ALL=C
TZ=UTC
export LC_ALL TZ

binary_input=${1:-./my_ls}
binary=$(cd "$(dirname "$binary_input")" && pwd)/$(basename "$binary_input")
if [ ! -x "$binary" ]; then
    printf 'test_ls: executable not found: %s\n' "$binary" >&2
    exit 1
fi

run_listing()
{
    status=0
    "$binary" "$@" >"$case_root/stdout" 2>"$case_root/stderr" || status=$?
}

expect_output()
{
    printf '%s' "$1" >"$case_root/expected"
    if ! cmp -s "$case_root/expected" "$case_root/stdout"; then
        printf 'unexpected stdout in %s\n' "$case_name" >&2
        diff -u "$case_root/expected" "$case_root/stdout" >&2 || :
        exit 1
    fi
}

expect_success()
{
    [ "$status" -eq 0 ]
    [ ! -s "$case_root/stderr" ]
}

# Break caught: omitting the implicit '.' operand or default name sorting.
no_operands()
{
    touch zebra alpha
    run_listing
    expect_success
    expect_output 'alpha
zebra
'
}

# Break caught: treating a regular file operand as a directory.
single_file_operand()
{
    touch sample
    run_listing sample
    expect_success
    expect_output 'sample
'
}

# Break caught: following argument order instead of sorting separate groups.
mixed_files_before_directory()
{
    mkdir zdir adir
    touch zdir/zchild zdir/achild adir/child zfile afile
    run_listing zdir zfile adir afile
    expect_success
    expect_output 'afile
zfile

adir:
child

zdir:
achild
zchild
'
}

# Break caught: counting valid directories rather than supplied operands.
multiple_operand_heading()
{
    mkdir beta alpha
    touch alpha/a beta/b
    run_listing beta alpha
    expect_success
    expect_output 'alpha:
a

beta:
b
'
}

# Break caught: printing unwanted headers or blank lines for one empty dir.
empty_directory()
{
    mkdir empty
    run_listing empty
    expect_success
    expect_output ''
    mkdir another
    run_listing empty another
    expect_success
    expect_output 'another:

empty:
'
}

# Break caught: failing to pass visibility options or effective uid defaults.
hidden_modes()
{
    mkdir sample
    touch sample/.hidden sample/visible
    run_listing sample
    expect_success
    if [ "$(id -u)" -eq 0 ]; then
        expect_output '.hidden
visible
'
    else
        expect_output 'visible
'
    fi
    run_listing -A sample
    expect_success
    expect_output '.hidden
visible
'
    run_listing -a sample
    expect_success
    expect_output '.
..
.hidden
visible
'
}

# Break caught: interpreting the actual leading-dash operand as an option.
operand_after_double_dash()
{
    touch ./-sample
    run_listing -- -sample
    expect_success
    expect_output '-sample
'
}

# Break caught: accepting an unknown option or writing diagnostics to stdout.
unknown_option()
{
    run_listing -Z
    [ "$status" -ne 0 ]
    expect_output ''
    grep -F 'illegal option -- Z' "$case_root/stderr" >/dev/null
    grep -F 'usage:' "$case_root/stderr" >/dev/null
}

# Break caught: returning early on a failed operand or losing error status.
missing_operand_continues()
{
    mkdir dir
    touch valid dir/inside
    run_listing absent valid dir
    [ "$status" -ne 0 ]
    expect_output 'valid

dir:
inside
'
    grep -F 'absent' "$case_root/stderr" >/dev/null
}

# Break caught: losing active sort modes when splitting operand groups.
reverse_groups()
{
    mkdir alpha beta
    touch alpha/a alpha/b beta/c afile zfile
    run_listing -r alpha afile beta zfile
    expect_success
    expect_output 'zfile
afile

beta:
c

alpha:
b
a
'
}

# Break caught: partitioning -d operands or sorting when -f is active.
directory_as_file_unsorted()
{
    mkdir alpha beta
    touch sample
    run_listing -df beta sample alpha
    expect_success
    expect_output 'beta
sample
alpha
'
}

# Break caught: suppressing default traversal of a directory symlink operand.
directory_symlink_operand()
{
    mkdir target
    touch target/child
    ln -s target linked
    run_listing linked
    expect_success
    expect_output 'child
'
}

# Break caught: ignoring classification, following a directory link with -F,
# or treating execute bits on a directory/FIFO/link as a regular-file marker.
# Sockets and platform whiteouts are covered by synthetic formatter entries.
classify_fixture()
{
    mkdir dir
    touch plain executable
    chmod 0644 plain
    chmod 0755 executable
    ln -s dir linked
    ln -s absent dangling
    mkfifo pipe
    chmod 0755 pipe
    run_listing -F
    expect_success
    expect_output 'dangling@
dir/
executable*
linked@
pipe|
plain
'
    run_listing -F linked
    expect_success
    expect_output 'linked@
'
    run_listing -dF dir
    expect_success
    expect_output 'dir/
'
}

expect_hex()
{
    actual_hex=$(od -An -tx1 "$case_root/stdout" | tr -d ' \n')
    if [ "$actual_hex" != "$1" ]; then
        printf 'unexpected output bytes in %s: %s\n' \
            "$case_name" "$actual_hex" >&2
        exit 1
    fi
}

# Break caught: ignoring -q or converting the stored operand before lookup.
q_replaces_control_byte()
{
    name=$(printf 'a\001b')
    touch "$name"
    run_listing -q "$name"
    expect_success
    expect_hex '613f620a'
}

# Break caught: sanitizing explicit raw output or changing the input path.
w_preserves_control_byte()
{
    name=$(printf 'a\001b')
    touch "$name"
    run_listing -w "$name"
    expect_success
    expect_hex '6101620a'
}

# TTY=true is exercised by the formatter unit suite; this CLI checks the
# actual redirected stdout default without depending on a pseudo-terminal.
terminal_default_is_unit_tested()
{
    name=$(printf 'a\001b')
    touch "$name"
    run_listing "$name"
    expect_success
    expect_hex '6101620a'
}

# Break caught: first-wins handling of either clustered or separate -q/-w.
q_w_rightmost()
{
    name=$(printf 'a\001b')
    touch "$name"
    run_listing -qw "$name"
    expect_success
    expect_hex '6101620a'
    run_listing -wq "$name"
    expect_success
    expect_hex '613f620a'
    run_listing -q -w "$name"
    expect_success
    expect_hex '6101620a'
    run_listing -w -q "$name"
    expect_success
    expect_hex '613f620a'
}

# Long cases use file operands so the later directory-total task can add
# its own coverage without changing these field expectations.
long_fixture()
{
    printf 'abc' > sample
    chmod 0644 sample
    touch -t 202601020304.00 sample
}

# Break caught: -n failing to enable long output or resolving account names.
numeric_long_fields()
{
    long_fixture
    run_listing -n sample
    expect_success
    expect_output "-rw-r--r-- 1 $(id -u) $(id -g) 3 Jan  2 03:04 sample
"
}

# Break caught: -l always printing numeric IDs despite real database names.
named_long_fields()
{
    long_fixture
    run_listing -l sample
    expect_success
    expect_output "-rw-r--r-- 1 $(id -un) $(id -gn) 3 Jan  2 03:04 sample
"
}

# Break caught: missing arrow, dereferencing a dangling link, or losing -F.
long_symlink_arrow()
{
    ln -s absent linked
    touch -h -t 202601020304.00 linked
    run_listing -nF linked
    expect_success
    expect_output "lrwxrwxrwx 1 $(id -u) $(id -g) 6 Jan  2 03:04 linked@ -> absent
"
}

# Break caught: defaulting to ctime/atime or substituting a year for HH:MM.
long_timestamp_mtime()
{
    long_fixture
    touch -a -t 202602030405.00 sample
    run_listing -n sample
    expect_success
    expect_output "-rw-r--r-- 1 $(id -u) $(id -g) 3 Jan  2 03:04 sample
"
}

# ctime cannot be assigned by touch. FreeBSD stat supplies the actual
# fixture ctime; mtime and atime remain independently assigned literals.
# Break caught: first-wins c/u selection or ignoring either selected time.
c_u_rightmost()
{
    long_fixture
    touch -a -t 202602030405.00 sample
    ctime=$(stat -f '%Sc' -t '%b %e %H:%M' sample)
    for flags in '-ncu' '-nc -u'; do
        # Intentional word splitting exercises separate option arguments.
        run_listing $flags sample
        expect_success
        expect_output "-rw-r--r-- 1 $(id -u) $(id -g) 3 Feb  3 04:05 sample
"
    done
    for flags in '-nuc' '-nu -c'; do
        run_listing $flags sample
        expect_success
        expect_output "-rw-r--r-- 1 $(id -u) $(id -g) 3 $ctime sample
"
    done
}

# Break caught: first-wins l/n or treating -l as disabling long output.
l_n_rightmost()
{
    long_fixture
    for flags in '-ln' '-l -n'; do
        run_listing $flags sample
        expect_success
        expect_output "-rw-r--r-- 1 $(id -u) $(id -g) 3 Jan  2 03:04 sample
"
    done
    for flags in '-nl' '-n -l'; do
        run_listing $flags sample
        expect_success
        expect_output "-rw-r--r-- 1 $(id -un) $(id -gn) 3 Jan  2 03:04 sample
"
    done
}

run_case()
(
    case_name=$1
    case_root=$(mktemp -d "${TMPDIR:-/tmp}/my_ls_cli.XXXXXX")
    case_root=$(cd "$case_root" && pwd)
    trap 'cd /; rm -rf "$case_root"' 0
    trap 'exit 1' HUP INT TERM
    mkdir "$case_root/data"
    cd "$case_root/data"
    "$case_name"
    printf 'PASS %s\n' "$case_name"
)

for case_name in no_operands single_file_operand mixed_files_before_directory \
    multiple_operand_heading empty_directory hidden_modes \
    operand_after_double_dash unknown_option missing_operand_continues \
    reverse_groups directory_as_file_unsorted directory_symlink_operand \
    classify_fixture q_replaces_control_byte w_preserves_control_byte \
    terminal_default_is_unit_tested q_w_rightmost numeric_long_fields \
    named_long_fields long_symlink_arrow long_timestamp_mtime \
    c_u_rightmost l_n_rightmost
do
    run_case "$case_name"
done
