#!/bin/sh
set -u

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
init_script="$repo_root/packages/keenetic/keen-pbr/files/opt/etc/init.d/S80keen-pbr"
tmp_script=$(mktemp)
tmp_config=$(mktemp)
trap 'rm -f "$tmp_script" "$tmp_config"' EXIT

# Load the real policy resolver functions without executing the init script's
# rc.func dispatch. Capability probing is replaced below with deterministic
# family results, so this remains a host-side policy matrix test.
awk '/^ipv6_runtime_enabled\(\)/ { in_helpers=1 }
     /^enable_hwnat\(\)/ { exit }
     in_helpers { print }' "$init_script" > "$tmp_script"
. "$tmp_script"

probe_case() {
    registered="$1"
    expected_reason="$2"
    probe_output=$(PROBE_REGISTERED="$registered" sh -c '
        . "$1"
        module_path_for() { printf "/missing/%s.ko\\n" "$1"; }
        insmod() { return 1; }
        grep() {
            case "$*" in
                *ip_tables_names*) [ "$PROBE_REGISTERED" = 1 ] && return 0 || return 1 ;;
                *) /bin/grep "$@" ;;
            esac
        }
        iptables() { return 1; }
        raw_family_available ipv4 && exit 1
        printf "%s\\n" "$RAW_FAMILY_PROBE_REASON"
    ' sh "$tmp_script")
    [ "$probe_output" = "$expected_reason" ] || {
        printf 'expected probe reason [%s], got [%s]\n' "$expected_reason" "$probe_output" >&2
        exit 1
    }
}

# A registered table with a failed command probe must report the authoritative
# probe failure even when the module file is absent. An unregistered table with
# no module reports the missing module instead.
probe_case 1 "raw registered in /proc/net/ip_tables_names but iptables -t raw -S failed"
probe_case 0 "module missing: /missing/iptable_raw.ko"

log() { :; }
log_error() { :; }
module_path_for() { printf '/missing/%s.ko\n' "$1"; }
insmod() { return 1; }
remove_daemon_arg() {
    target="$1"
    filtered=
    for arg in $ARGS; do
        [ "$arg" = "$target" ] || filtered="$filtered $arg"
    done
    ARGS="$filtered"
}
ipv6_runtime_enabled() { return 0; }
raw_family_available() {
    RAW_FAMILY_PROBE_REASON="mocked probe unavailable"
    [ "$1" = ipv6 ] && [ "${RAW6_AVAILABLE:-0}" = 1 ] && return 0
    [ "$1" = ipv4 ] && [ "${RAW4_AVAILABLE:-0}" = 1 ] && return 0
    return 1
}

assert_args() {
    expected="$1"
    # Normalize the resolver's intentionally space-prefixed ARGS value.
    actual=$(printf '%s\n' "$ARGS" | awk '{$1=$1; print}')
    [ "$actual" = "$expected" ] || {
        printf 'expected [%s], got [%s]\n' "$expected" "$actual" >&2
        exit 1
    }
}

run_case() {
    mode="$1"; RAW4_AVAILABLE="$2"; RAW6_AVAILABLE="$3"; config_value="$4"; expected="$5"
    if [ "$config_value" = false ]; then
        printf '{"daemon":{"ipv6_enabled":false}}\n' > "$tmp_config"
    else
        printf '{"daemon":{"ipv6_enabled":true}}\n' > "$tmp_config"
    fi
    CONFIG="$tmp_config"
    KEEN_PBR_RAW_PREROUTING="$mode"
    ARGS="--base --use-raw-prerouting --use-raw6-prerouting"
    enable_raw_prerouting_if_available
    assert_args "$expected"
}

run_case auto 1 1 true  "--base --use-raw-prerouting --use-raw6-prerouting"
run_case auto 1 0 true  "--base --use-raw-prerouting"
run_case auto 0 1 true  "--base --use-raw6-prerouting"
run_case auto 0 0 true  "--base"
run_case enable 1 1 true "--base --use-raw-prerouting --use-raw6-prerouting"
run_case enable 0 0 true "--base --use-raw-prerouting --use-raw6-prerouting"
run_case disable 1 1 true "--base"
run_case ipv4-only 1 0 true "--base --use-raw-prerouting"
run_case ipv6-only 0 1 true "--base --use-raw6-prerouting"
run_case ipv6-only 0 0 false "--base"

printf '%s\n' 'Keenetic RAW policy matrix: PASS'
