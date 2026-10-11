#!/bin/sh
set -u

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
init_script="$repo_root/packages/keenetic/keen-pbr/files/opt/etc/init.d/S80keen-pbr"
tmp_script=$(mktemp)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_script" "$tmp_dir"' EXIT

# Load only the module helpers; sysfs, insmod and logging are replaced below,
# so nothing touches the real system.
awk '/^(kernel_module_present|load_kernel_module|report_module_failure|ensure_intercept_modules_loaded)\(\)/ { keep=1 }
     keep { print }
     keep && /^}/ { keep=0 }' "$init_script" > "$tmp_script"
. "$tmp_script"

KERNEL_MODULE_SYSFS="$tmp_dir/sys"
mkdir -p "$KERNEL_MODULE_SYSFS/nfnetlink_log" "$tmp_dir/lib"
LOGGED=""
module_path_for() { printf '%s/lib/%s.ko\n' "$tmp_dir" "$1"; }
insmod() {
    case "$1" in
        */xt_NFLOG.ko) echo "Unknown symbol in module" >&2; return 1 ;;
    esac
    # Runs in a command substitution, so record calls in a file.
    printf ' %s' "$(basename "$1" .ko)" >> "$tmp_dir/inserted"
}
log() { LOGGED="$LOGGED|info:$1"; }
log_warn() { LOGGED="$LOGGED|warn:$1"; }
log_error() { LOGGED="$LOGGED|error:$1"; }

fail() { printf '%s: [%s]\n' "$1" "$LOGGED" >&2; exit 1; }

# nfnetlink_log is loaded, xt_conntrack is built in (listed in the matches
# file), nfnetlink_queue and xt_NFQUEUE load, xt_NFLOG fails to load and
# xt_connbytes has no .ko at all.
: > "$tmp_dir/lib/nfnetlink_queue.ko"
: > "$tmp_dir/lib/xt_NFQUEUE.ko"
: > "$tmp_dir/lib/xt_NFLOG.ko"
printf 'conntrack\n' > "$tmp_dir/matches"
sed "s#/proc/net/ip_tables_matches#$tmp_dir/matches#g" "$tmp_script" > "$tmp_script.2"
. "$tmp_script.2"
rm -f "$tmp_script.2"

ensure_intercept_modules_loaded || fail "must never fail"

inserted=$(cat "$tmp_dir/inserted")
[ "$inserted" = " nfnetlink_queue xt_NFQUEUE" ] ||
    fail "unexpected insmod calls$inserted"
case "$LOGGED" in
    *"info:Loaded kernel module $tmp_dir/lib/nfnetlink_queue.ko"*"info:Loaded kernel module $tmp_dir/lib/xt_NFQUEUE.ko"*) ;;
    *) fail "missing load messages" ;;
esac
case "$LOGGED" in
    *"warn:failed to load kernel module $tmp_dir/lib/xt_NFLOG.ko (Unknown symbol in module)"*"warn:kernel module xt_connbytes is not loaded"*) ;;
    *) fail "missing warnings" ;;
esac
case "$LOGGED" in
    *nfnetlink_log*|*xt_conntrack*|*error:*) fail "unexpected message" ;;
esac

# Required modules log errors; probe modules leave reporting to the caller.
LOGGED=""
load_kernel_module required xt_set "" "routing breaks"
[ $? = 1 ] || fail "missing module must return 1"
case "$LOGGED" in
    "|error:kernel module xt_set is not loaded and $tmp_dir/lib/xt_set.ko does not exist: routing breaks") ;;
    *) fail "required failure must be an error" ;;
esac
LOGGED=""
: > "$tmp_dir/lib/xt_NFLOG.ko"
load_kernel_module probe xt_NFLOG "" ""
[ $? = 2 ] || fail "insmod failure must return 2"
[ -z "$LOGGED" ] || fail "probe failures must be silent"

printf '%s\n' 'Keenetic kernel module loading: PASS'
