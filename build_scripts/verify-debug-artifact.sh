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

anchor=$($nm_tool -n "$debug" | awk '$3 == "keen_pbr_crash_symbolization_anchor" { print $1; exit }')
test -n "$anchor" || fail "symbolization anchor is missing"
location=$($addr2line_tool -e "$debug" "0x$anchor" | head -n 1)
case "$location" in
    "??:0"|"??:?"|"") fail "symbolization anchor has no source line" ;;
esac

debuglink_file=$(mktemp)
trap 'rm -f "$debuglink_file"' EXIT
$objcopy_tool --dump-section .gnu_debuglink="$debuglink_file" "$binary" 2>/dev/null || true
test -s "$debuglink_file" || fail "deployed binary has no .gnu_debuglink"

echo "verified debug artifact: build-id=$binary_build_id anchor=$location"
