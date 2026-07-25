#!/bin/sh
set -eu

if [ "$#" -ne 6 ]; then
    echo "usage: $0 <binary> <debug> <readelf> <nm> <addr2line> <objcopy>" >&2
    exit 2
fi

binary=$1
debug=$2
readelf_tool=$3
nm_tool=$4
addr2line_tool=$5
objcopy_tool=$6

fail() {
    echo "debug artifact verification failed: $*" >&2
    exit 1
}

test -s "$binary" || fail "binary is missing or empty: $binary"
test -s "$debug" || fail "debug ELF is missing or empty: $debug"

binary_build_id=$($readelf_tool -n "$binary" 2>/dev/null | awk '/Build ID:/ { print $3; exit }')
debug_build_id=$($readelf_tool -n "$debug" 2>/dev/null | awk '/Build ID:/ { print $3; exit }')
test -n "$binary_build_id" || fail "binary has no GNU build ID"
test "$binary_build_id" = "$debug_build_id" || fail "binary/debug build IDs differ"

$readelf_tool -S "$binary" 2>/dev/null | grep -q '[.]eh_frame' || fail "deployed binary has no .eh_frame"
$readelf_tool -S "$debug" 2>/dev/null | grep -q '[.]debug_info' || fail "debug ELF has no .debug_info"
$readelf_tool -S "$debug" 2>/dev/null | grep -Eq '[.]debug_line([[:space:]]|$)' || fail "debug ELF has no .debug_line"

anchor_record=$(
    "$nm_tool" -S -n "$debug" |
        awk '$4 == "keen_pbr_crash_symbolization_anchor" {
            print $1, $2
            exit
        }'
)

set -- $anchor_record
anchor=${1:-}
anchor_size=${2:-}

test -n "$anchor" || fail "symbolization anchor is missing"
test -n "$anchor_size" || fail "symbolization anchor has no size"

anchor_size_dec=$(printf '%d' "0x$anchor_size")
offset=0
location=
resolved_anchor=

while [ "$offset" -lt "$anchor_size_dec" ]; do
    candidate=$(printf '0x%x' "$((0x$anchor + offset))")
    candidate_location=$(
        "$addr2line_tool" -e "$debug" "$candidate" |
            head -n 1
    )

    case "$candidate_location" in
        "??:0"|"??:?"|"")
            ;;
        *)
            location=$candidate_location
            resolved_anchor=$candidate
            break
            ;;
    esac

    offset=$((offset + 1))
done

test -n "$location" ||
    fail "symbolization anchor has no source line: address=0x$anchor size=0x$anchor_size"

debuglink_file=$(mktemp)
trap 'rm -f "$debuglink_file"' EXIT
$objcopy_tool --dump-section .gnu_debuglink="$debuglink_file" "$binary" 2>/dev/null || true
test -s "$debuglink_file" || fail "deployed binary has no .gnu_debuglink"

echo "verified debug artifact: build-id=$binary_build_id anchor=$resolved_anchor location=$location"
