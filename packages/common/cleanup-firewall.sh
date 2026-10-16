#!/bin/sh
# Removes every firewall object keen-pbr owns: the iptables chains KeenPbr*
# (and the rules that jump to them), the nftables table inet KeenPbrTable and
# the ipsets kpbr*.  The package scripts run it on removal, after the service
# is stopped.  A clean stop already removes all of this; the script covers a
# daemon that crashed or never started, whose leftover rules (marking, DNS
# hold) would keep steering traffic after keen-pbr is gone.  Uses only
# iptables, nft and ipset, never keen-pbr itself.  Idempotent; always exits 0.
#
#   cleanup-firewall.sh [--reason uninstall|stop|crash]
#
# The service scripts run it after every stop and the daemon's crash handler
# runs it before the process dies.  Finding anything is not normal (a clean
# stop leaves nothing), so that is logged as a warning naming the reason.
#
# keen-pbr's policy routing rules only match its fwmarks, so once nothing marks
# packets they are inert and are left alone (they disappear on reboot).

IPTABLES="${IPTABLES:-iptables}"
IP6TABLES="${IP6TABLES:-ip6tables}"
NFT="${NFT:-nft}"
IPSET="${IPSET:-ipset}"
XT_TABLES="${XT_TABLES:-mangle raw}"
NFT_TABLE="KeenPbrTable"
REASON="uninstall"
[ "${1:-}" = --reason ] && [ -n "${2:-}" ] && REASON="$2"
# Set to 1 once anything was removed.
found=0

log() {
    found=1
    logger -t keen-pbr "cleanup-firewall: $1" >/dev/null 2>&1 || true
}

# cleanup_xt_table <command> <table>
cleanup_xt_table() {
    rules="$("$1" -t "$2" -S 2>/dev/null)" || return 0
    owned="$(printf '%s\n' "$rules" | sed -n 's/^-N \(KeenPbr[^ ]*\)$/\1/p')"
    # Every chain that is not ours: built-in (-P) and user chains (-N).
    others="$(printf '%s\n' "$rules" | sed -n 's/^-[PN] \([^ ]*\).*$/\1/p' | grep -v '^KeenPbr')"
    # Jumps into our chains are deleted by number, highest first, so the
    # remaining numbers stay valid.  -L prints "num target ..." per rule.
    for chain in $others; do
        nums="$("$1" -t "$2" -L "$chain" -n --line-numbers 2>/dev/null |
            awk '$1 ~ /^[0-9]+$/ && $2 ~ /^KeenPbr/ { print $1 }' | sort -rn)"
        for num in $nums; do
            "$1" -t "$2" -D "$chain" "$num" 2>/dev/null &&
                log "$1 -t $2: removed rule $num of $chain"
        done
    done
    # Flush all first: our chains may jump to each other.
    for chain in $owned; do
        "$1" -t "$2" -F "$chain" 2>/dev/null
    done
    for chain in $owned; do
        "$1" -t "$2" -X "$chain" 2>/dev/null && log "$1 -t $2: removed chain $chain"
    done
}

for cmd in "$IPTABLES" "$IP6TABLES"; do
    command -v "$cmd" >/dev/null 2>&1 || continue
    for table in $XT_TABLES; do
        cleanup_xt_table "$cmd" "$table"
    done
done

if command -v "$NFT" >/dev/null 2>&1 &&
   "$NFT" list table inet "$NFT_TABLE" >/dev/null 2>&1; then
    "$NFT" delete table inet "$NFT_TABLE" >/dev/null 2>&1 &&
        log "removed nftables table inet $NFT_TABLE"
fi

# After the rules: a set still referenced by a rule cannot be destroyed.
if command -v "$IPSET" >/dev/null 2>&1; then
    for set in $("$IPSET" list -n 2>/dev/null | grep '^kpbr'); do
        "$IPSET" destroy "$set" >/dev/null 2>&1 && log "removed ipset $set"
    done
fi

if [ "$found" = 1 ]; then
    logger -s -t keen-pbr -p user.warning \
        "WARNING: keen-pbr firewall rules were left behind (on $REASON: keen-pbr did not stop cleanly); removed them" \
        2>&1 || true
fi

exit 0
