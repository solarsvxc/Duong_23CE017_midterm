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
symlink_directory_default()
{
    mkdir target
    touch target/child
    ln -s target linked
    run_listing linked
    expect_success
    expect_output 'child
'
    run_listing -F linked
    expect_success
    expect_output 'child
'
    rm target/child
    for flags in '-l' '-n' '-lF' '-nF'; do
        run_listing "$flags" linked
        expect_success
        expect_output 'total 0
'
    done
}

# Break caught: ignoring classification, suppressing operand traversal with -F,
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
    expect_output ''
    run_listing -dF dir
    expect_success
    expect_output 'dir/
'
}

# Break caught: opening a -d operand or printing directory section headers.
d_lists_directory_operand()
{
    mkdir beta alpha
    touch alpha/inside sample
    run_listing -d beta sample alpha
    expect_success
    expect_output 'alpha
beta
sample
'
    run_listing -d
    expect_success
    expect_output '.
'
}

# Break caught: replacing -d symlink metadata with the directory target.
d_does_not_dereference_symlink()
{
    mkdir target
    touch target/child
    ln -s target linked
    touch -h -t 202601020304.00 linked
    run_listing -dF linked
    expect_success
    expect_output 'linked@
'
    run_listing -dn linked
    expect_success
    expect_output "lrwxrwxrwx 1 $(id -u) $(id -g) 6 Jan  2 03:04 linked -> target
"
}

# Break caught: breadth-first traversal, omitted/duplicate directories,
# missing single-operand headings, or recursing before the parent listing.
recursive_depth_first_sections()
{
    mkdir -p tree/alpha/deep tree/beta
    touch tree/rootfile tree/alpha/afile tree/alpha/deep/leaf tree/beta/bfile
    run_listing -R tree
    expect_success
    expect_output 'tree:
alpha
beta
rootfile

tree/alpha:
afile
deep

tree/alpha/deep:
leaf

tree/beta:
bfile
'
    run_listing -Rr tree
    expect_success
    expect_output 'tree:
rootfile
beta
alpha

tree/beta:
bfile

tree/alpha:
deep
afile

tree/alpha/deep:
leaf
'
}

# Break caught: recursing into visible '.'/'..' entries or skipping hidden
# real children when -a makes them visible.
recursive_never_visits_dot_entries()
{
    mkdir -p tree/.hidden tree/child
    run_listing -aR tree
    expect_success
    expect_output 'tree:
.
..
.hidden
child

tree/.hidden:
.
..

tree/child:
.
..
'
}

# Break caught: following a child symlink instead of its own lstat type;
# command-line links must still traverse the same real tree.
recursive_does_not_follow_child_symlink()
{
    mkdir -p tree/child outside
    touch outside/external tree/child/leaf
    ln -s ../outside tree/linked
    ln -s tree operand
    run_listing -RF operand
    expect_success
    expect_output 'operand:
child/
linked@

operand/child:
leaf
'
}

# Break caught: following a child link back to an ancestor and visiting it
# repeatedly instead of listing the link exactly once.
symlink_cycle_not_followed()
{
    mkdir -p tree/child
    ln -s .. tree/child/up
    run_listing -R tree
    expect_success
    expect_output 'tree:
child

tree/child:
up
'
}

# Break caught: first-wins or clustered-only -R/-d option handling.
R_d_rightmost()
{
    mkdir -p tree/child
    for flags in '-Rd' '-R -d'; do
        # Intentional splitting checks both clustered and separate options.
        run_listing $flags tree
        expect_success
        expect_output 'tree
'
    done
    for flags in '-dR' '-d -R'; do
        run_listing $flags tree
        expect_success
        expect_output 'tree:
child

tree/child:
'
    done
}

# Break caught: abandoning siblings/remaining operands after opendir fails,
# discarding the failure status, or emitting directory errors on stdout.
unreadable_child_continues_when_not_root()
{
    if [ "$(id -u)" -eq 0 ]; then
        printf 'SKIP unreadable child: root bypasses directory permissions\n'
        return
    fi
    mkdir -p tree/alpha tree/zeta zzlater
    touch tree/alpha/hidden tree/zeta/leaf zzlater/last
    chmod 000 tree/alpha
    run_listing -R tree zzlater
    chmod 700 tree/alpha
    [ "$status" -ne 0 ]
    grep -F 'tree/alpha' "$case_root/stderr" >/dev/null
    expect_output 'tree:
alpha
zeta

tree/alpha:

tree/zeta:
leaf

zzlater:
last
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

# Break caught: omitting the inode or printing it after the name.
inode_prefix()
{
    touch sample
    inode=$(stat -f '%i' sample)
    run_listing -i sample
    expect_success
    expect_output "$inode sample
"
}

# Native allocation is filesystem-dependent; stat supplies raw 512B blocks.
# Break caught: ignoring BLOCKSIZE, not rounding, or letting it override -k.
blocksize_512_and_1k()
{
    printf 'abc' > sample
    blocks=$(stat -f '%b' sample)
    BLOCKSIZE=512
    export BLOCKSIZE
    run_listing -s sample
    expect_success
    expect_output "$blocks sample
"
    BLOCKSIZE=1K
    run_listing -s sample
    expect_success
    expect_output "$(((blocks + 1) / 2)) sample
"
    BLOCKSIZE=1M
    run_listing -s sample
    expect_success
    expect_output "$(((blocks + 2047) / 2048)) sample
"
    run_listing -sk sample
    expect_success
    expect_output "$(((blocks + 1) / 2)) sample
"
}

# Break caught: ignoring the rightmost size option or using BLOCKSIZE for -h.
h_overrides_k()
{
    truncate -s 1536 sample
    chmod 0644 sample
    touch -t 202601020304.00 sample
    BLOCKSIZE=1M
    export BLOCKSIZE
    run_listing -nkh sample
    expect_success
    expect_output "-rw-r--r-- 1 $(id -u) $(id -g) 1.5K Jan  2 03:04 sample
"
    run_listing -nhk sample
    expect_success
    expect_output "-rw-r--r-- 1 $(id -u) $(id -g) 1536 Jan  2 03:04 sample
"
    run_listing -skh sample
    expect_success
    expect_output '0B sample
'
}

# Break caught: applying allocated block units to logical file size.
k_does_not_change_long_size()
{
    long_fixture
    run_listing -nk sample
    expect_success
    expect_output "-rw-r--r-- 1 $(id -u) $(id -g) 3 Jan  2 03:04 sample
"
}

# Break caught: suppressing totals when no entries exist or -s is absent.
long_total_even_empty()
{
    mkdir empty
    run_listing -n empty
    expect_success
    expect_output 'total 0
'
    run_listing -nh empty
    expect_success
    expect_output 'total 0B
'
}

# Actual stdout is redirected here; the unit fixture exercises TTY=true.
s_total_only_for_tty_via_formatter()
{
    mkdir sample
    touch sample/child
    run_listing -s sample
    expect_success
    expect_output '0 child
'
}

# Break caught: human mode replacing device IDs with st_size.
device_major_minor_when_available()
{
    if [ ! -c /dev/null ]; then
        printf 'SKIP device fixture: /dev/null unavailable\n'
        return
    fi
    run_listing -nh /dev/null
    expect_success
    grep -E '^c[^ ]* +[0-9]+ +[0-9]+ +[0-9]+ +[0-9]+, +[0-9]+ ' \
        "$case_root/stdout" >/dev/null
}

# Break caught: narrowing st_size to 32 bits or converting it under -n.
large_sparse_file()
{
    truncate -s 5368709120 sample
    chmod 0644 sample
    touch -t 202601020304.00 sample
    run_listing -n sample
    expect_success
    expect_output "-rw-r--r-- 1 $(id -u) $(id -g) 5368709120 Jan  2 03:04 sample
"
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
    reverse_groups directory_as_file_unsorted symlink_directory_default \
    d_lists_directory_operand d_does_not_dereference_symlink \
    recursive_depth_first_sections recursive_never_visits_dot_entries \
    recursive_does_not_follow_child_symlink symlink_cycle_not_followed \
    R_d_rightmost unreadable_child_continues_when_not_root \
    classify_fixture q_replaces_control_byte w_preserves_control_byte \
    terminal_default_is_unit_tested q_w_rightmost numeric_long_fields \
    named_long_fields long_symlink_arrow long_timestamp_mtime \
    c_u_rightmost l_n_rightmost inode_prefix blocksize_512_and_1k \
    h_overrides_k k_does_not_change_long_size long_total_even_empty \
    s_total_only_for_tty_via_formatter device_major_minor_when_available \
    large_sparse_file
do
    run_case "$case_name"
done
