#!/bin/sh
set -u

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
init_script="$repo_root/packages/keenetic/keen-pbr/files/opt/etc/init.d/S80keen-pbr"
tmp_script=$(mktemp)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_script" "$tmp_dir"' EXIT

# Load only the module helpers; lsmod, insmod and logging are replaced below,
# so nothing touches the real system.
awk '/^(is_module_loaded|try_kernel_module_loaded|ensure_intercept_modules_loaded)\(\)/ { keep=1 }
     keep { print }
     keep && /^}/ { keep=0 }' "$init_script" > "$tmp_script"
. "$tmp_script"

LOADED="nfnetlink_log"
INSERTED=""
LOGGED=""
lsmod() { for m in $LOADED; do printf '%s 16384 0\n' "$m"; done; }
module_path_for() { printf '%s/%s.ko\n' "$tmp_dir" "$1"; }
insmod() {
    case "$1" in
        */xt_NFLOG.ko) return 1 ;;   # present but fails to load
    esac
    INSERTED="$INSERTED $(basename "$1" .ko)"
}
log() { LOGGED="$LOGGED|$1"; }

# nfnetlink_queue and xt_NFQUEUE are available, xt_NFLOG fails to load and
# xt_connbytes has no .ko at all.
: > "$tmp_dir/nfnetlink_queue.ko"
: > "$tmp_dir/xt_NFQUEUE.ko"
: > "$tmp_dir/xt_NFLOG.ko"

ensure_intercept_modules_loaded || { echo "must never fail" >&2; exit 1; }

[ "$INSERTED" = " nfnetlink_queue xt_NFQUEUE" ] || {
    printf 'unexpected insmod calls [%s]\n' "$INSERTED" >&2
    exit 1
}
case "$LOGGED" in
    *"module xt_NFLOG is not loaded"*"module xt_connbytes is not loaded"*) ;;
    *) printf 'missing warnings: [%s]\n' "$LOGGED" >&2; exit 1 ;;
esac
case "$LOGGED" in
    *nfnetlink_log*|*nfnetlink_queue*|*xt_NFQUEUE*)
        printf 'unexpected warning: [%s]\n' "$LOGGED" >&2; exit 1 ;;
esac

printf '%s\n' 'Keenetic intercept module loading: PASS'
