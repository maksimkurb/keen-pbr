#!/bin/sh
# Functional test for packages/common/cleanup-firewall.sh with fake iptables,
# nft and ipset commands.

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
script="$repo_root/packages/common/cleanup-firewall.sh"
tmp_root=$(mktemp -d "${TMPDIR:-/tmp}/keen-pbr-cleanup-firewall.XXXXXX")
trap 'rm -rf "$tmp_root"' EXIT HUP INT TERM

fail() {
    echo "cleanup-firewall test: $*" >&2
    exit 1
}

sh -n "$script" || fail "syntax error in $script"

bin="$tmp_root/bin"
mkdir -p "$bin"

# Fake iptables: the state of table T is "$FAKE_XT_DIR/<command>.<T>" in
# `iptables -S` format.  Supports -S, -L <chain> -n --line-numbers,
# -D <chain> <num>, -F <chain>, -X <chain>.
cat > "$bin/fake-xt" <<'FAKE'
#!/bin/sh
name="$(basename "$0")"
[ "$1" = -t ] || exit 2
db="$FAKE_XT_DIR/$name.$2"
shift 2
[ -f "$db" ] || { echo "table does not exist" >&2; exit 1; }
case "$1" in
    -S) cat "$db" ;;
    -L)
        echo "Chain $2"
        echo "num  target  prot opt source destination"
        awk -v c="$2" '$1 == "-A" && $2 == c {
            n++; t = "";
            for (i = 3; i < NF; i++) if ($i == "-j" || $i == "-g") t = $(i + 1)
            print n "    " t "  all  --  0.0.0.0/0  0.0.0.0/0"
        }' "$db"
        ;;
    -D)
        awk -v c="$2" -v k="$3" '$1 == "-A" && $2 == c && ++n == k { next } { print }' "$db" > "$db.new"
        cat "$db.new" > "$db"
        ;;
    -F)
        awk -v c="$2" '!($1 == "-A" && $2 == c)' "$db" > "$db.new"
        cat "$db.new" > "$db"
        ;;
    -X)
        grep -q "^-A .* -[jg] $2\( \|$\)" "$db" && { echo "chain in use" >&2; exit 1; }
        grep -vxF -- "-N $2" "$db" > "$db.new" || true
        cat "$db.new" > "$db"
        ;;
    *) exit 2 ;;
esac
FAKE
chmod +x "$bin/fake-xt"
ln -s fake-xt "$bin/iptables"
ln -s fake-xt "$bin/ip6tables"

cat > "$bin/nft" <<'FAKE'
#!/bin/sh
echo "$*" >> "$FAKE_LOG"
case "$*" in
    "list table inet KeenPbrTable") [ -f "$FAKE_XT_DIR/nft-table" ] ;;
    "delete table inet KeenPbrTable") rm -f "$FAKE_XT_DIR/nft-table" ;;
    *) exit 1 ;;
esac
FAKE
cat > "$bin/ipset" <<'FAKE'
#!/bin/sh
echo "$*" >> "$FAKE_LOG"
sets="$FAKE_XT_DIR/ipsets"
case "$1" in
    list) cat "$sets" ;;
    destroy) grep -vxF "$2" "$sets" > "$sets.new" || true; cat "$sets.new" > "$sets" ;;
    *) exit 1 ;;
esac
FAKE
cat > "$bin/logger" <<'FAKE'
#!/bin/sh
echo "logger $*" >> "$FAKE_LOG"
FAKE
chmod +x "$bin/nft" "$bin/ipset" "$bin/logger"

export FAKE_XT_DIR="$tmp_root/state" FAKE_LOG="$tmp_root/calls.log"
mkdir -p "$FAKE_XT_DIR"
: > "$FAKE_LOG"

cat > "$FAKE_XT_DIR/iptables.mangle" <<'EOF'
-P PREROUTING ACCEPT
-P OUTPUT ACCEPT
-P POSTROUTING ACCEPT
-N KeenPbrTable
-N KeenPbrTable_A
-N KeenPbrDnsHold
-N UserChain
-A PREROUTING -i br0 -j UserChain
-A PREROUTING -j KeenPbrTable
-A PREROUTING -p tcp -j ACCEPT
-A PREROUTING -m comment --comment "keen-pbr $(x)" -j KeenPbrTable
-A OUTPUT -j KeenPbrTable
-A POSTROUTING -p udp --dport 53 -j KeenPbrDnsHold
-A UserChain -j KeenPbrTable_A
-A KeenPbrTable -j KeenPbrTable_A
-A KeenPbrTable_A -m set --match-set kpbr4_lan dst -j MARK --set-xmark 0x10000/0xff0000
-A KeenPbrDnsHold -j NFQUEUE --queue-num 0
EOF
cat > "$FAKE_XT_DIR/iptables.raw" <<'EOF'
-P PREROUTING ACCEPT
-N KeenPbrRaw
-A PREROUTING -j KeenPbrRaw
-A KeenPbrRaw -j MARK --set-xmark 0x1/0x1
EOF
cat > "$FAKE_XT_DIR/iptables.nat" <<'EOF'
-P POSTROUTING ACCEPT
-N KeenPbrSnat
-N _NDM_MASQ
-A POSTROUTING -j _NDM_MASQ
-A POSTROUTING -j KeenPbrSnat
-A KeenPbrSnat -m mark --mark 0x20000/0xff0000 -j MASQUERADE
-A _NDM_MASQ -s 192.168.1.0/24 -j MASQUERADE
EOF
# ip6tables has mangle only; its raw and nat tables do not exist.
cat > "$FAKE_XT_DIR/ip6tables.mangle" <<'EOF'
-P PREROUTING ACCEPT
-N KeenPbrTable
-A PREROUTING -j KeenPbrTable
EOF
touch "$FAKE_XT_DIR/nft-table"
printf 'kpbr4_lan\nuser_set\nkpbr6t_vpn\n' > "$FAKE_XT_DIR/ipsets"

PATH="$bin:$PATH" sh "$script" --reason stop || fail "script failed"
grep -q 'logger -s .*user.warning .*left behind (on stop:' "$FAKE_LOG" ||
    fail "leftovers were not reported as a warning naming the reason"

expected_mangle='-P PREROUTING ACCEPT
-P OUTPUT ACCEPT
-P POSTROUTING ACCEPT
-N UserChain
-A PREROUTING -i br0 -j UserChain
-A PREROUTING -p tcp -j ACCEPT'
[ "$(cat "$FAKE_XT_DIR/iptables.mangle")" = "$expected_mangle" ] ||
    fail "iptables mangle not cleaned: $(cat "$FAKE_XT_DIR/iptables.mangle")"
[ "$(cat "$FAKE_XT_DIR/iptables.raw")" = '-P PREROUTING ACCEPT' ] || fail "iptables raw not cleaned"
expected_nat='-P POSTROUTING ACCEPT
-N _NDM_MASQ
-A POSTROUTING -j _NDM_MASQ
-A _NDM_MASQ -s 192.168.1.0/24 -j MASQUERADE'
[ "$(cat "$FAKE_XT_DIR/iptables.nat")" = "$expected_nat" ] ||
    fail "iptables nat not cleaned: $(cat "$FAKE_XT_DIR/iptables.nat")"
[ "$(cat "$FAKE_XT_DIR/ip6tables.mangle")" = '-P PREROUTING ACCEPT' ] || fail "ip6tables mangle not cleaned"
[ ! -e "$FAKE_XT_DIR/nft-table" ] || fail "nft table not deleted"
grep -qxF 'destroy kpbr4_lan' "$FAKE_LOG" || fail "ipset kpbr4_lan not destroyed"
grep -qxF 'destroy kpbr6t_vpn' "$FAKE_LOG" || fail "ipset kpbr6t_vpn not destroyed"
! grep -q 'destroy user_set' "$FAKE_LOG" || fail "destroyed a foreign ipset"

# Second run: nothing left to do, still succeeds.
cp "$FAKE_XT_DIR/iptables.mangle" "$tmp_root/mangle.before"
: > "$FAKE_LOG"
PATH="$bin:$PATH" sh "$script" --reason stop || fail "second run failed"
cmp -s "$FAKE_XT_DIR/iptables.mangle" "$tmp_root/mangle.before" || fail "second run changed iptables"
! grep -q 'left behind' "$FAKE_LOG" || fail "warned although nothing was left"

# No firewall tools at all: still exits 0.
mkdir -p "$tmp_root/empty"
PATH="$tmp_root/empty" IPTABLES=none IP6TABLES=none NFT=none IPSET=none \
    /bin/sh "$script" || fail "run without tools failed"

echo "cleanup-firewall tests: PASS"
