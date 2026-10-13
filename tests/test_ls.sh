#!/bin/sh
# Each case uses real operands and exact, independently derived output.
set -eu
LC_ALL=C
TZ=UTC
BLOCKSIZE=512
export LC_ALL TZ BLOCKSIZE

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
    expect_output "$(stat -f '%Sp' linked) 1 $(id -u) $(id -g) 6 Jan  2 03:04 linked -> target
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

# Break caught: printing raw directory operands in multiple-directory headings.
# Lookup must still use the newline/control-byte names under -q.
multiple_directory_heading_bytes()
{
    name=$(printf 'a\001\nb')
    mkdir "$name" z
    touch "$name/leaf"
    run_listing -q "$name" z
    expect_success
    expect_hex '613f3f623a0a6c6561660a0a7a3a0a'
    run_listing -w "$name" z
    expect_success
    expect_hex '61010a623a0a6c6561660a0a7a3a0a'
}

# Break caught: sanitizing entries but bypassing name policy on recursive paths,
# or passing a sanitized heading back to directory traversal.
recursive_heading_bytes()
{
    name=$(printf 'a\001\nb')
    child=$(printf 'c\002\nd')
    mkdir -p "$name/$child"
    touch "$name/$child/leaf"
    run_listing -qR "$name"
    expect_success
    expect_hex '613f3f623a0a633f3f640a0a613f3f622f633f3f643a0a6c6561660a'
    run_listing -wR "$name"
    expect_success
    expect_hex '61010a623a0a63020a640a0a61010a622f63020a643a0a6c6561660a'
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
    expect_output "$(stat -f '%Sp' linked) 1 $(id -u) $(id -g) 6 Jan  2 03:04 linked@ -> absent
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
    # Filesystems may allocate blocks for a truncate-created file. Compare
    # against -sh so this checks option precedence without assuming st_blocks.
    run_listing -sh sample
    expect_success
    cp "$case_root/stdout" "$case_root/expected"
    run_listing -skh sample
    expect_success
    if ! cmp -s "$case_root/expected" "$case_root/stdout"; then
        printf 'unexpected stdout in %s\n' "$case_name" >&2
        diff -u "$case_root/expected" "$case_root/stdout" >&2 || :
        exit 1
    fi
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

# Break caught: an accepted option that has no effect at its public consumer.
# Reuse behavioral fixtures in isolated directories, rather than smoke-testing
# a successful parse. Every synopsis letter has an explicit fixture below.
every_option_is_accepted()
{
    for option in A a c d F f h i k l n q R r S s t u w; do
        (
            mkdir "option_$option"
            cd "option_$option"
            case "$option" in
                A|a) hidden_modes ;;
                c|u) time_cli_mtime_ctime_atime ;;
                d) d_lists_directory_operand ;;
                F) classify_fixture ;;
                f|r|S|t) sort_cli_name_size_time_reverse_none ;;
                h) h_overrides_k ;;
                i) inode_prefix ;;
                k|s) blocksize_512_and_1k ;;
                l) named_long_fields ;;
                n) numeric_long_fields ;;
                q) q_replaces_control_byte ;;
                R) recursive_depth_first_sections ;;
                w) w_preserves_control_byte ;;
            esac
        )
    done
}

# Break caught: losing hidden visibility, inode fields, named long format,
# or classification when these options are used together.
combined_AilF()
{
    mkdir listing
    printf 'abc' > listing/.hidden
    chmod 0755 listing/.hidden
    touch -t 202601020304.00 listing/.hidden
    inode=$(stat -f '%i' listing/.hidden)
    blocks=$(stat -f '%b' listing/.hidden)
    run_listing -AilF listing
    expect_success
    expect_output "total $blocks
$inode -rwxr-xr-x 1 $(id -un) $(id -gn) 3 Jan  2 03:04 .hidden*
"
}

# Break caught: numeric/human/prefix formatting discarding time sorting or
# reverse order, or prefixing logical sizes instead of allocated bytes.
combined_nhstr()
{
    truncate -s 1536 alpha
    truncate -s 2048 beta
    chmod 0644 alpha beta
    touch -t 202601020304.00 alpha
    touch -t 202602030405.00 beta
    # Allocated blocks differ by filesystem; isolate each expected prefix.
    run_listing -sh alpha
    expect_success
    alpha_blocks=$(sed -n '1s/ .*//p' "$case_root/stdout")
    run_listing -sh beta
    expect_success
    beta_blocks=$(sed -n '1s/ .*//p' "$case_root/stdout")
    run_listing -nhstr beta alpha
    expect_success
    expect_output "$alpha_blocks -rw-r--r-- 1 $(id -u) $(id -g) 1.5K Jan  2 03:04 alpha
$beta_blocks -rw-r--r-- 1 $(id -u) $(id -g) 2.0K Feb  3 04:05 beta
"
}

sort_fixture()
{
    truncate -s 3 alpha
    truncate -s 1 beta
    truncate -s 2 gamma
    chmod 0644 alpha beta gamma
    touch -m -t 202601020304.00 alpha
    touch -m -t 202601030405.00 beta
    touch -m -t 202601010203.00 gamma
    touch -a -t 202602010203.00 alpha
    touch -a -t 202602020304.00 beta
    touch -a -t 202602030405.00 gamma
}

# Break caught: wrong primary key, reverse direction, or sorting SORT_NONE.
# Explicit operands make unsorted order independent of readdir/filesystems.
sort_cli_name_size_time_reverse_none()
{
    sort_fixture
    for flags in '' '-S' '-t' '-r' '-Sr' '-tr' '-f' '-fr'; do
        run_listing $flags gamma beta alpha
        expect_success
        case "$flags" in
            '') expected=$(printf 'alpha\nbeta\ngamma') ;;
            -S) expected=$(printf 'alpha\ngamma\nbeta') ;;
            -t) expected=$(printf 'beta\nalpha\ngamma') ;;
            -r) expected=$(printf 'gamma\nbeta\nalpha') ;;
            -Sr) expected=$(printf 'beta\ngamma\nalpha') ;;
            -tr) expected=$(printf 'gamma\nalpha\nbeta') ;;
            -f|-fr) expected=$(printf 'gamma\nbeta\nalpha') ;;
        esac
        expect_output "$expected
"
    done
    # The same keys must sort collected directory contents.
    mkdir listing
    mv alpha beta gamma listing/
    for flags in '' '-S' '-t' '-r'; do
        run_listing $flags listing
        expect_success
        case "$flags" in
            '') expected=$(printf 'alpha\nbeta\ngamma') ;;
            -S) expected=$(printf 'alpha\ngamma\nbeta') ;;
            -t) expected=$(printf 'beta\nalpha\ngamma') ;;
            -r) expected=$(printf 'gamma\nbeta\nalpha') ;;
        esac
        expect_output "$expected
"
    done
}

# Break caught: using mtime for every selected timestamp in sorting/output.
# ctime cannot be set directly: separated chmod operations give a known order.
time_cli_mtime_ctime_atime()
{
    sort_fixture
    chmod 0600 beta
    sleep 1
    chmod 0600 alpha
    sleep 1
    chmod 0600 gamma
    for flags in '-t' '-tc' '-tu'; do
        run_listing "$flags" alpha gamma beta
        expect_success
        case "$flags" in
            -t) expected=$(printf 'beta\nalpha\ngamma') ;;
            -tc) expected=$(printf 'gamma\nalpha\nbeta') ;;
            -tu) expected=$(printf 'gamma\nbeta\nalpha') ;;
        esac
        expect_output "$expected
"
    done
    ctime=$(stat -f '%Sc' -t '%b %e %H:%M' alpha)
    for flags in '-n' '-nc' '-nu'; do
        run_listing "$flags" alpha
        expect_success
        case "$flags" in
            -n) timestamp='Jan  2 03:04' ;;
            -nc) timestamp=$ctime ;;
            -nu) timestamp='Feb  1 02:03' ;;
        esac
        expect_output "-rw------- 1 $(id -u) $(id -g) 3 $timestamp alpha
"
    done
}

# Break caught: first-wins pair selection, or applying it only to clusters.
# Existing cases assert both directions of all five manual-defined pairs.
# Add all six ordered primary-sort pairs, clustered and separate, here.
all_rightmost_pairs_cli()
{
    for pair_case in l_n_rightmost c_u_rightmost R_d_rightmost \
        h_overrides_k q_w_rightmost; do
        (
            mkdir "$pair_case"
            cd "$pair_case"
            "$pair_case"
        )
    done
    sort_fixture
    for flags in '-fS' '-f -S' '-Sf' '-S -f' '-ft' '-f -t' \
        '-tf' '-t -f' '-St' '-S -t' '-tS' '-t -S'; do
        run_listing $flags gamma beta alpha
        expect_success
        case "$flags" in
            '-fS'|'-f -S'|'-tS'|'-t -S')
                expected=$(printf 'alpha\ngamma\nbeta') ;;
            '-ft'|'-f -t'|'-St'|'-S -t')
                expected=$(printf 'beta\nalpha\ngamma') ;;
            *) expected=$(printf 'gamma\nbeta\nalpha') ;;
        esac
        expect_output "$expected
"
    done
    # h/k must also honor separate arguments in both directions.
    for flags in '-n -k -h' '-n -h -k'; do
        run_listing $flags alpha
        expect_success
        case "$flags" in
            '-n -k -h') size=3B ;;
            *) size=3 ;;
        esac
        expect_output "-rw-r--r-- 1 $(id -u) $(id -g) $size Jan  2 03:04 alpha
"
    done
}

# Break caught: headings based on surviving operands after a failed lookup.
failed_operand_still_requires_heading()
{
    mkdir directory
    touch directory/child
    run_listing absent directory
    [ "$status" -ne 0 ]
    grep -F 'my_ls: absent:' "$case_root/stderr" >/dev/null
    expect_output 'directory:
child
'
}

# Break caught: unstable partition reversing directory operands in -f mode.
# Empty directories make the assertion independent of readdir order.
unsorted_multi_directory_partition()
{
    mkdir zdir adir mdir
    touch zfile afile
    run_listing -f zdir zfile adir afile mdir
    expect_success
    expect_output 'zfile
afile

zdir:

adir:

mdir:
'
}

# Break caught: swallowing errors or suppressing the remaining valid output.
error_exit_status()
{
    touch valid
    run_listing valid
    expect_success
    expect_output 'valid
'
    run_listing absent valid
    [ "$status" -ne 0 ]
    expect_output 'valid
'
    grep -F 'my_ls: absent:' "$case_root/stderr" >/dev/null
    run_listing absent
    [ "$status" -ne 0 ]
    expect_output ''
    grep -F 'my_ls: absent:' "$case_root/stderr" >/dev/null
    run_listing -Z valid
    [ "$status" -ne 0 ]
    expect_output ''
    grep -F 'illegal option -- Z' "$case_root/stderr" >/dev/null
    grep -F 'usage:' "$case_root/stderr" >/dev/null
}

# Break caught: clean leaving build artifacts or a rebuild missing objects.
# Build in a private source copy; never remove the running suite's binaries.
clean_rebuild()
{
    project_root=$(dirname "$binary")
    rebuild_root="$case_root/rebuild"
    mkdir "$rebuild_root"
    cp "$project_root/Makefile" "$rebuild_root/"
    cp -R "$project_root/include" "$project_root/src" \
        "$project_root/tests" "$rebuild_root/"
    (
        cd "$rebuild_root"
        touch src/cleanup_probe.o tests/cleanup_probe.o
        "${MAKE:-make}" clean
        for artifact in my_ls src/main.o src/options.o src/entry.o \
            src/sort.o src/format.o tests/test_options tests/test_entry \
            tests/test_sort tests/test_format tests/test_main; do
            [ ! -e "$artifact" ]
        done
        [ -z "$(find src tests -name '*.o' -print)" ]
        "${MAKE:-make}" all unit-test
        [ -x my_ls ]
        for object in src/main.o src/options.o src/entry.o src/sort.o \
            src/format.o; do
            [ -f "$object" ]
        done
        # Check rebuilt CLI behavior without recursively invoking make test.
        mkdir fixture
        touch fixture/zeta fixture/alpha
        ./my_ls fixture >"$case_root/rebuilt_stdout" \
            2>"$case_root/rebuilt_stderr"
        printf 'alpha\nzeta\n' >"$case_root/rebuilt_expected"
        cmp -s "$case_root/rebuilt_expected" "$case_root/rebuilt_stdout"
        [ ! -s "$case_root/rebuilt_stderr" ]
        "${MAKE:-make}" clean
        for artifact in my_ls src/main.o src/options.o src/entry.o \
            src/sort.o src/format.o tests/test_options tests/test_entry \
            tests/test_sort tests/test_format tests/test_main; do
            [ ! -e "$artifact" ]
        done
        [ -z "$(find src tests -name '*.o' -print)" ]
    )
}

cleanup_case()
{
    # Restore search permission even if an unreadable fixture aborts early.
    if [ -d "$case_root/data/tree/alpha" ]; then
        chmod 700 "$case_root/data/tree/alpha" || :
    fi
    cd /
    rm -rf "$case_root"
}

run_case()
(
    case_name=$1
    case_root=$(mktemp -d "${TMPDIR:-/tmp}/my_ls_cli.XXXXXX")
    case_root=$(cd "$case_root" && pwd)
    trap 'cleanup_case' 0
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
    multiple_directory_heading_bytes recursive_heading_bytes \
    terminal_default_is_unit_tested q_w_rightmost numeric_long_fields \
    named_long_fields long_symlink_arrow long_timestamp_mtime \
    c_u_rightmost l_n_rightmost inode_prefix blocksize_512_and_1k \
    h_overrides_k k_does_not_change_long_size long_total_even_empty \
    s_total_only_for_tty_via_formatter device_major_minor_when_available \
    large_sparse_file every_option_is_accepted combined_AilF combined_nhstr \
    sort_cli_name_size_time_reverse_none time_cli_mtime_ctime_atime \
    all_rightmost_pairs_cli failed_operand_still_requires_heading \
    unsorted_multi_directory_partition error_exit_status clean_rebuild
do
    run_case "$case_name"
done
