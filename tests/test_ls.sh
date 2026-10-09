#!/bin/sh
# Each case uses real operands and exact, independently derived output.
set -eu
LC_ALL=C
export LC_ALL

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
    reverse_groups directory_as_file_unsorted directory_symlink_operand
do
    run_case "$case_name"
done
