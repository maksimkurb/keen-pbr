#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_root=$(mktemp -d "${TMPDIR:-/tmp}/keen-pbr-dnsmasq-mig.XXXXXX")
trap 'rm -rf "$tmp_root"' EXIT HUP INT TERM

fail() {
    echo "dnsmasq migration test: $*" >&2
    exit 1
}

# Test Keenetic dnsmasq.conf migration
test_keenetic_migration() {
    local dnsmasq_conf="$tmp_root/keenetic-dnsmasq.conf"
    local fallback_conf="$tmp_root/keenetic-fallback.conf"

    # Prepare fallback config
    mkdir -p "$(dirname "$fallback_conf")"
    printf '%s\n' 'server=8.8.8.8' 'server=8.8.4.4' > "$fallback_conf"

    # Test 1: managed block + no servers → block removed, fallback added
    cat > "$dnsmasq_conf" <<'EOF'
# Some existing config
# BEGIN keen-pbr managed block
# Do not remove the following line, it is required for keen-pbr to function properly.
conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry
# END keen-pbr managed block
# More config
EOF
    DNSMASQ_CONF="$dnsmasq_conf" FALLBACK_CONF="$fallback_conf" RESTART_CMD="true" \
        "$repo_root/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/migrate-dnsmasq.sh"

    ! grep -q "# BEGIN keen-pbr managed block" "$dnsmasq_conf" || fail "Test 1: Managed block was not removed"
    grep -q "# BEGIN keen-pbr fallback upstream" "$dnsmasq_conf" || fail "Test 1: Fallback block was not added"
    grep -q "server=8.8.8.8" "$dnsmasq_conf" || fail "Test 1: Fallback servers not added"
    grep -q "# Some existing config" "$dnsmasq_conf" || fail "Test 1: Existing config was lost"

    # Test 2: legacy bare conf-script line → removed
    cat > "$dnsmasq_conf" <<'EOF'
user=dnsmasq
conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry
server=1.1.1.1
EOF
    DNSMASQ_CONF="$dnsmasq_conf" FALLBACK_CONF="$fallback_conf" RESTART_CMD="true" \
        "$repo_root/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/migrate-dnsmasq.sh"

    ! grep -q "dnsmasq-config-entry" "$dnsmasq_conf" || fail "Test 2: conf-script line was not removed"
    grep -q "user=dnsmasq" "$dnsmasq_conf" || fail "Test 2: user line was lost"
    grep -q "server=1.1.1.1" "$dnsmasq_conf" || fail "Test 2: server line was lost"

    # Test 3: user servers present → no fallback added
    cat > "$dnsmasq_conf" <<'EOF'
# BEGIN keen-pbr managed block
conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry
# END keen-pbr managed block
server=192.168.1.1
EOF
    DNSMASQ_CONF="$dnsmasq_conf" FALLBACK_CONF="$fallback_conf" RESTART_CMD="true" \
        "$repo_root/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/migrate-dnsmasq.sh"

    ! grep -q "# BEGIN keen-pbr managed block" "$dnsmasq_conf" || fail "Test 3: Managed block was not removed"
    ! grep -q "# BEGIN keen-pbr fallback upstream" "$dnsmasq_conf" || fail "Test 3: Fallback block should not be added"
    grep -q "server=192.168.1.1" "$dnsmasq_conf" || fail "Test 3: User server was lost"

    # Test 4: second run → no change
    local before before_size
    before=$(cat "$dnsmasq_conf")
    before_size=$(wc -c < "$dnsmasq_conf")
    DNSMASQ_CONF="$dnsmasq_conf" FALLBACK_CONF="$fallback_conf" RESTART_CMD="true" \
        "$repo_root/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/migrate-dnsmasq.sh"

    [ "$(cat "$dnsmasq_conf")" = "$before" ] || fail "Test 4: Second migration changed the file"
}

# --- OpenWrt: functional test with a fake uci -------------------------------

uci_sh="$repo_root/packages/openwrt/keen-pbr/files/usr/lib/keen-pbr/uci.sh"

setup_fake_uci() {
    fake_bin="$tmp_root/fakebin"
    mkdir -p "$fake_bin"
    cat > "$fake_bin/uci" <<'EOF'
#!/bin/sh
# Minimal uci: state is a text file with "key=value" lines, one per list item;
# section types are stored as "package.section=type".
db="$FAKE_UCI_DB"
[ "$1" = "-q" ] && shift
cmd="$1"
arg="$2"
case "$cmd" in
    show)
        grep "^${arg}\." "$db" || true
        ;;
    get)
        grep "^${arg}=" "$db" | sed "s/^${arg}=//" | tr '\n' ' ' | sed 's/ $//' > "$db.get"
        [ -s "$db.get" ] || exit 1
        cat "$db.get"; echo
        ;;
    add_list)
        printf '%s\n' "$arg" >> "$db"
        ;;
    del_list)
        grep -vxF "$arg" "$db" > "$db.new" || true
        cat "$db.new" > "$db"
        ;;
    delete)
        grep -v "^${arg}=" "$db" > "$db.new" || true
        cat "$db.new" > "$db"
        ;;
    commit)
        echo "commit $arg" >> "$FAKE_UCI_LOG"
        ;;
    *)
        echo "fake uci: unsupported: $*" >&2
        exit 1
        ;;
esac
EOF
    chmod +x "$fake_bin/uci"
}

run_openwrt_migration() {
    FAKE_UCI_DB="$tmp_root/uci.db" FAKE_UCI_LOG="$tmp_root/uci.log" \
    DNSMASQ_RESTART_CMD="echo restart >> $tmp_root/restart.log" \
    PATH="$fake_bin:$PATH" sh "$uci_sh" dnsmasq-migrate-from-keen-pbr >/dev/null 2>&1 ||
        fail "uci.sh dnsmasq-migrate-from-keen-pbr exited non-zero"
}

count_lines() {
    if [ -f "$1" ]; then wc -l < "$1" | tr -d ' '; else echo 0; fi
}

test_openwrt_migration() {
    sh -n "$uci_sh" || fail "uci.sh syntax check failed"
    setup_fake_uci

    confdir="$tmp_root/dnsmasq.d"
    mkdir -p "$confdir"
    printf 'conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq' > "$confdir/keen-pbr.conf"
    : > "$tmp_root/restart.log"
    : > "$tmp_root/uci.log"

    # (a) integrated section: server + kpbr_server (one duplicate), our mounts and a foreign one
    cat > "$tmp_root/uci.db" <<EOF
dhcp.cfg01=dnsmasq
dhcp.cfg01.confdir=$confdir
dhcp.cfg01.server=9.9.9.9
dhcp.cfg01.server=1.1.1.1
dhcp.cfg01.kpbr_server=1.1.1.1
dhcp.cfg01.kpbr_server=8.8.8.8
dhcp.cfg01.kpbr_server=/example.org/10.0.0.1
dhcp.cfg01.addnmount=/usr/sbin/keen-pbr
dhcp.cfg01.addnmount=/opt/foreign
dhcp.cfg01.addnmount=/etc/keen-pbr
dhcp.cfg01.addnmount=/var/cache/keen-pbr
dhcp.cfg01.addnmount=/var/run/keen-pbr
EOF
    run_openwrt_migration

    servers=$(grep '^dhcp.cfg01.server=' "$tmp_root/uci.db" | sed 's/.*=//' | tr '\n' ' ')
    [ "$servers" = "9.9.9.9 1.1.1.1 8.8.8.8 /example.org/10.0.0.1 " ] || fail "(a) unexpected server list: $servers"
    ! grep -q 'kpbr_server' "$tmp_root/uci.db" || fail "(a) kpbr_server not deleted"
    [ "$(grep '^dhcp.cfg01.addnmount=' "$tmp_root/uci.db")" = "dhcp.cfg01.addnmount=/opt/foreign" ] ||
        fail "(a) addnmount cleanup wrong"
    [ ! -e "$confdir/keen-pbr.conf" ] || fail "(a) keen-pbr.conf not deleted"
    [ "$(count_lines "$tmp_root/uci.log")" = 1 ] || fail "(a) expected exactly one commit"
    [ "$(count_lines "$tmp_root/restart.log")" = 1 ] || fail "(a) expected exactly one dnsmasq restart"

    # (b) second run: nothing to do
    cp "$tmp_root/uci.db" "$tmp_root/uci.db.before"
    run_openwrt_migration
    cmp -s "$tmp_root/uci.db" "$tmp_root/uci.db.before" || fail "(b) second run changed the config"
    [ "$(count_lines "$tmp_root/uci.log")" = 1 ] || fail "(b) second run committed"
    [ "$(count_lines "$tmp_root/restart.log")" = 1 ] || fail "(b) second run restarted dnsmasq"

    # (c) foreign section: no kpbr_server, foreign addnmount, foreign confdir file
    printf 'conf-script=/something/else\n' > "$confdir/keen-pbr.conf"
    cat > "$tmp_root/uci.db" <<EOF
dhcp.cfg02=dnsmasq
dhcp.cfg02.confdir=$confdir
dhcp.cfg02.server=1.1.1.1
dhcp.cfg02.addnmount=/opt/foreign
dhcp.cfg02.addnmount=/opt/other
EOF
    cp "$tmp_root/uci.db" "$tmp_root/uci.db.before"
    run_openwrt_migration
    cmp -s "$tmp_root/uci.db" "$tmp_root/uci.db.before" || fail "(c) foreign config was modified"
    [ -e "$confdir/keen-pbr.conf" ] || fail "(c) foreign drop-in was deleted"
    [ "$(count_lines "$tmp_root/uci.log")" = 1 ] || fail "(c) committed"
    [ "$(count_lines "$tmp_root/restart.log")" = 1 ] || fail "(c) restarted dnsmasq"
}

echo "Testing Keenetic dnsmasq.conf migration..."
test_keenetic_migration

echo "Testing OpenWrt uci.sh dnsmasq migration..."
test_openwrt_migration

echo "dnsmasq migration tests: PASS"
