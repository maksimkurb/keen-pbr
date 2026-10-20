#!/bin/sh
# Functional tests for the per-platform dnsmasq hooks (apply|remove|uninstall|install-recommended-config|status|alive).

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_root=$(mktemp -d "${TMPDIR:-/tmp}/keen-pbr-dnsmasq-hooks.XXXXXX")
trap 'rm -rf "$tmp_root"' EXIT HUP INT TERM

fail() {
    echo "dnsmasq hooks test: $*" >&2
    exit 1
}

owrt_hook="$repo_root/packages/openwrt/keen-pbr/files/usr/lib/keen-pbr/dnsmasq-hook.sh"
keen_dir="$repo_root/packages/keenetic/keen-pbr/files/opt"
keen_hook="$keen_dir/usr/lib/keen-pbr/dnsmasq-hook.sh"
keen_sh="$keen_dir/usr/lib/keen-pbr/dnsmasq.sh"
keen_recommended="$keen_dir/usr/lib/keen-pbr/dnsmasq.recommended.conf"
owrt_init="$repo_root/packages/openwrt/keen-pbr/files/etc/init.d/keen-pbr-dnsmasq"
deb_sh="$repo_root/packages/debian/files/usr/lib/keen-pbr/dnsmasq.sh"
deb_recommended="$repo_root/packages/debian/files/usr/lib/keen-pbr/dnsmasq.recommended.conf"
deb_hook="$repo_root/packages/debian/files/usr/lib/keen-pbr/dnsmasq-hook.sh"

for f in "$owrt_hook" "$keen_hook" "$keen_sh" "$deb_hook" "$deb_sh"; do
    sh -n "$f" || fail "syntax error in $f"
done

restarts() {
    if [ -f "$tmp_root/restart.log" ]; then wc -l < "$tmp_root/restart.log" | tr -d ' '; else echo 0; fi
}
reset_restarts() { : > "$tmp_root/restart.log"; }
commits() {
    if [ -f "$tmp_root/uci.log" ]; then wc -l < "$tmp_root/uci.log" | tr -d ' '; else echo 0; fi
}
# Number of files among the arguments modified after 2001 (the tests reset
# mtimes to 2000 and check that hooks do not rewrite unchanged files).
touched() {
    n=0
    for p in "$@"; do
        if [ -n "$(find "$p" -newermt 2001-01-01 2>/dev/null)" ]; then n=$((n + 1)); fi
    done
    echo "$n"
}
export RESTART_CMD="echo restart >> $tmp_root/restart.log"

# Fake uci: one "package.section.option=value" line per list entry.
fake_bin="$tmp_root/fakebin"
mkdir -p "$fake_bin"
cat > "$fake_bin/uci" <<'FAKE'
#!/bin/sh
db="$FAKE_UCI_DB"
# Like real uci, "show" prints anonymous (cfg*) sections as @type[N] unless -X.
extended=0
while :; do
    case "$1" in
        -q) shift ;;
        -X) extended=1; shift ;;
        *) break ;;
    esac
done
cmd="$1"
arg="$2"
case "$cmd" in
    show)
        if [ "$extended" = 1 ]; then
            grep "^${arg}\." "$db" || true
        else
            awk -v pkg="$arg" '
                index($0, pkg ".") != 1 { next }
                {
                    rest = substr($0, length(pkg) + 2)
                    sec = rest; sub(/[.=].*/, "", sec)
                    if (rest == sec "=" substr(rest, length(sec) + 2) && !(sec in name) && sec ~ /^cfg/) {
                        type = substr(rest, length(sec) + 2)
                        name[sec] = "@" type "[" (n[type]++ + 0) "]"
                    }
                    if (sec in name) rest = name[sec] substr(rest, length(sec) + 1)
                    print pkg "." rest
                }' "$db"
        fi
        ;;
    get)
        grep "^${arg}=" "$db" | sed "s/^${arg}=//" | tr '\n' ' ' | sed 's/ $//' > "$db.get"
        [ -s "$db.get" ] || exit 1
        cat "$db.get"; echo
        ;;
    add_list) printf '%s\n' "$arg" >> "$db" ;;
    del_list)
        grep -vxF "$arg" "$db" > "$db.new" || true
        cat "$db.new" > "$db"
        ;;
    delete)
        grep -v "^${arg}=" "$db" > "$db.new" || true
        cat "$db.new" > "$db"
        ;;
    commit) echo "commit $arg" >> "$FAKE_UCI_LOG" ;;
    changes) [ -n "${FAKE_UCI_CHANGES:-}" ] && printf '%s\n' "$FAKE_UCI_CHANGES"; exit 0 ;;
    *) echo "fake uci: unsupported: $*" >&2; exit 1 ;;
esac
FAKE
chmod +x "$fake_bin/uci"

# Exit status of a command (the tests run with set -e).
rc_of() {
    _rc=0
    "$@" >/dev/null 2>&1 || _rc=$?
    echo "$_rc"
}

# Fake pidof: finds a process only when FAKE_PIDOF=1.
cat > "$fake_bin/pidof" <<'FAKE'
#!/bin/sh
[ "${FAKE_PIDOF:-0}" = 1 ] && { echo 1234; exit 0; }
exit 1
FAKE
chmod +x "$fake_bin/pidof"

# --- OpenWrt -----------------------------------------------------------------

owrt_setup() {
    rm -rf "$tmp_root/owrt"
    confdir="$tmp_root/owrt/tmp/dnsmasq.cfg01.d"
    guestdir="$tmp_root/owrt/x/guest.d"
    export FAKE_UCI_DB="$tmp_root/uci.db" FAKE_UCI_LOG="$tmp_root/uci.log"
    export TMP_ROOT="$tmp_root/owrt/tmp"
    export CACHE_DIR="$tmp_root/owrt/cache" CONFIG_DIR="$tmp_root/owrt/etc-keen-pbr"
    export LEGACY_RUN_DIR="$tmp_root/owrt/run-keen-pbr"
    export KEEN_PBR_BIN=/usr/sbin/keen-pbr
    export LOCK_DIR="$tmp_root/hook.lock" DNSMASQ_BIN=/usr/sbin/dnsmasq
    unset INIT_SCRIPT
    : > "$tmp_root/uci.log"
    run() { PATH="$fake_bin:$PATH" UCI="$fake_bin/uci" sh "$owrt_hook" "$@"; }
    f1="$confdir/keen-pbr.conf"
    f2="$guestdir/keen-pbr.conf"
    want='conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq'
}

test_openwrt() {
    owrt_setup
    cat > "$FAKE_UCI_DB" <<EOT
dhcp.cfg01=dnsmasq
dhcp.cfg01.addnmount=$CONFIG_DIR
dhcp.cfg01.server=1.1.1.1
dhcp.cfg01.server=/lan/192.168.1.1
dhcp.cfg01.server=/local/
dhcp.guest=dnsmasq
dhcp.guest.confdir=$guestdir,*.conf
EOT

    [ "$(run status)" = not-installed ] || fail "owrt: status before apply"
    out=$(run inspect) || fail "owrt: inspect failed"
    printf '%s\n' "$out" | grep -qxF 'hook_version=2' || fail "owrt: inspect hook_version"
    [ "$(printf '%s\n' "$out" | grep -cxF '[instance]')" = 2 ] || fail "owrt: inspect instance count"
    printf '%s\n' "$out" | grep -qxF 'selector=uci:cfg01' || fail "owrt: inspect selector"
    printf '%s\n' "$out" | grep -qxF "dropin_dir=$guestdir" || fail "owrt: inspect did not strip the confdir filter"
    printf '%s\n' "$out" | grep -qxF "dropin_file=$f1" || fail "owrt: inspect drop-in name"
    ! printf '%s\n' "$out" | grep -q 'owned_mounts' || fail "owrt: inspect still reports an ownership list"
    [ "$(printf '%s\n' "$out" | grep -cxF 'dropin_state=absent')" = 2 ] || fail "owrt: inspect state before apply"

    # Usage and selector errors.
    reset_restarts
    [ "$(rc_of run apply)" = 64 ] || fail "owrt: apply without selector should exit 64"
    [ "$(rc_of run remove)" = 64 ] || fail "owrt: remove without selector should exit 64"
    [ "$(rc_of run apply uci:nosuch)" = 3 ] || fail "owrt: unknown selector should exit 3"
    [ "$(rc_of run apply cfg01)" = 3 ] || fail "owrt: selector without scheme should exit 3"
    [ ! -e "$f1" ] && [ ! -e "$f2" ] && [ "$(restarts)" = 0 ] || fail "owrt: failed apply touched something"

    # apply with default upstreams: unscoped servers move, scoped ones stay.
    run apply uci:cfg01 --default-upstreams=yes || fail "owrt: apply failed"
    [ "$(cat "$f1")" = "$want" ] || fail "owrt: conf file 1 wrong"
    [ ! -e "$f2" ] || fail "owrt: apply wrote into another section's confdir"
    [ ! -d "$TMP_ROOT/dnsmasq.guest.d" ] || fail "owrt: apply created the guest default dir"
    [ "$(grep -c "^dhcp.cfg01.addnmount=$CONFIG_DIR\$" "$FAKE_UCI_DB")" = 1 ] || fail "owrt: pre-existing mount duplicated"
    grep -qxF "dhcp.cfg01.addnmount=/usr/sbin/keen-pbr" "$FAKE_UCI_DB" || fail "owrt: bin mount missing"
    grep -qxF "dhcp.cfg01.addnmount=$CACHE_DIR" "$FAKE_UCI_DB" || fail "owrt: cache mount missing"
    [ "$(grep -c '^dhcp.cfg01.addnmount=' "$FAKE_UCI_DB")" = 3 ] || fail "owrt: addnmount should list exactly three paths"
    ! grep -q '^dhcp.guest.addnmount' "$FAKE_UCI_DB" || fail "owrt: guest got mounts"
    ! grep -q 'keen_pbr_addnmount' "$FAKE_UCI_DB" || fail "owrt: ownership list written"
    grep -qxF "dhcp.cfg01.kpbr_server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt: unscoped server not stashed"
    ! grep -qxF "dhcp.cfg01.server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt: unscoped server still in server"
    grep -qxF "dhcp.cfg01.server=/lan/192.168.1.1" "$FAKE_UCI_DB" || fail "owrt: scoped server /lan left server"
    grep -qxF "dhcp.cfg01.server=/local/" "$FAKE_UCI_DB" || fail "owrt: scoped server /local/ left server"
    ! grep -q "^dhcp.cfg01.kpbr_server=/" "$FAKE_UCI_DB" || fail "owrt: scoped server stashed"
    [ "$(restarts)" = 1 ] || fail "owrt: apply should restart once"
    [ "$(tail -n 1 "$tmp_root/restart.log")" = "restart cfg01" ] || fail "owrt: restart should name the section"
    [ "$(commits)" = 1 ] || fail "owrt: apply should commit once"
    [ "$(run status)" = installed ] || fail "owrt: status after apply"
    out=$(run inspect)
    printf '%s\n' "$out" | grep -qxF 'dropin_state=current' || fail "owrt: inspect state after apply"
    [ "$(printf '%s\n' "$out" | grep -cxF 'dropin_state=absent')" = 1 ] || fail "owrt: guest state after apply"
    [ ! -e "$LOCK_DIR" ] || fail "owrt: lock left behind"

    # Second apply: no rewrite, no commit, no UCI change; the restart still happens.
    touch -t 200001010000 "$f1"
    cp "$FAKE_UCI_DB" "$tmp_root/uci.before"
    run apply uci:cfg01 --default-upstreams=yes || fail "owrt: second apply failed"
    [ "$(touched "$f1")" = 0 ] || fail "owrt: second apply rewrote the conf file"
    cmp -s "$FAKE_UCI_DB" "$tmp_root/uci.before" || fail "owrt: second apply changed uci"
    [ "$(commits)" = 1 ] || fail "owrt: second apply committed"
    [ "$(restarts)" = 2 ] || fail "owrt: second apply should restart again"

    # PER_INSTANCE_RESTART=0 restarts without a section name.
    PER_INSTANCE_RESTART=0 run apply uci:cfg01 --default-upstreams=yes || fail "owrt: apply (restart all) failed"
    [ "$(tail -n 1 "$tmp_root/restart.log")" = "restart" ] || fail "owrt: PER_INSTANCE_RESTART=0 restart line"

    # No default upstreams (or no flag at all): the servers come back, one commit.
    c=$(commits)
    run apply uci:cfg01 --default-upstreams=no || fail "owrt: apply (no upstreams) failed"
    grep -qxF "dhcp.cfg01.server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt: server not returned"
    ! grep -q '^dhcp.cfg01.kpbr_server' "$FAKE_UCI_DB" || fail "owrt: stash not emptied"
    [ "$(commits)" = "$((c + 1))" ] || fail "owrt: returning the servers should commit once"
    c=$(commits)
    run apply uci:cfg01 || fail "owrt: apply (no flag) failed"
    [ "$(commits)" = "$c" ] || fail "owrt: apply without a flag committed"
    grep -qxF "dhcp.cfg01.server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt: apply without a flag moved servers"

    # Remove (integration off): servers back, mounts and drop-in stay.
    run apply uci:cfg01 --default-upstreams=yes || fail "owrt: re-apply failed"
    c=$(commits); n=$(restarts)
    run remove uci:cfg01 || fail "owrt: remove failed"
    grep -qxF "dhcp.cfg01.server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt: remove did not return the server"
    ! grep -q '^dhcp.cfg01.kpbr_server' "$FAKE_UCI_DB" || fail "owrt: remove left the stash"
    [ -f "$f1" ] || fail "owrt: remove deleted the drop-in"
    [ "$(grep -c '^dhcp.cfg01.addnmount=' "$FAKE_UCI_DB")" = 3 ] || fail "owrt: remove changed the mounts"
    [ "$(commits)" = "$((c + 1))" ] || fail "owrt: remove should commit once"
    [ "$(restarts)" = "$((n + 1))" ] || fail "owrt: remove should restart once"
    [ "$(tail -n 1 "$tmp_root/restart.log")" = "restart cfg01" ] || fail "owrt: remove should restart the instance only"

    # Remove with nothing to return: no commit, still a restart.
    cp "$FAKE_UCI_DB" "$tmp_root/uci.before"
    c=$(commits); n=$(restarts)
    run remove uci:cfg01 || fail "owrt: idempotent remove failed"
    [ "$(commits)" = "$c" ] || fail "owrt: idempotent remove committed"
    cmp -s "$FAKE_UCI_DB" "$tmp_root/uci.before" || fail "owrt: idempotent remove changed uci"
    [ "$(restarts)" = "$((n + 1))" ] || fail "owrt: remove should restart"

    # Foreign drop-in is never overwritten.
    guest_before=$(cat "$FAKE_UCI_DB")
    mkdir -p "$guestdir"
    echo other > "$f2"
    rc=$(rc_of run apply uci:guest)
    [ "$rc" = 1 ] || fail "owrt: foreign drop-in should exit 1 (got $rc)"
    [ "$(cat "$f2")" = other ] || fail "owrt: foreign drop-in overwritten"
    [ "$(cat "$FAKE_UCI_DB")" = "$guest_before" ] || fail "owrt: foreign drop-in changed uci"
    [ "$(run inspect | grep -cxF 'dropin_state=foreign')" = 1 ] || fail "owrt: inspect foreign state"
    rm -f "$f2"

    # Section with a confdir filter: the file goes into the bare directory.
    run apply uci:guest --default-upstreams=yes || fail "owrt: apply guest failed"
    [ "$(cat "$f2")" = "$want" ] || fail "owrt: guest drop-in not in the stripped confdir"

    # Pending foreign UCI changes: refuse to edit and commit.
    run remove uci:guest || fail "owrt: remove guest failed"
    c=$(commits); cp "$FAKE_UCI_DB" "$tmp_root/uci.before"
    rm -f "$f1"
    rc=$(FAKE_UCI_CHANGES="dhcp.cfg01.foo=bar" rc_of run apply uci:cfg01 --default-upstreams=yes)
    [ "$rc" = 1 ] || fail "owrt: pending uci changes should exit 1 (got $rc)"
    [ "$(commits)" = "$c" ] || fail "owrt: committed over pending changes"
    cmp -s "$FAKE_UCI_DB" "$tmp_root/uci.before" || fail "owrt: pending-changes apply changed uci"
    [ ! -e "$f1" ] || fail "owrt: pending-changes apply wrote the drop-in"

    # Lock: live owner -> exit 4; dead owner -> stolen and released.
    mkdir "$LOCK_DIR"; echo $$ > "$LOCK_DIR/pid"
    rc=$(LOCK_TRIES=1 rc_of run apply uci:cfg01)
    [ "$rc" = 4 ] || fail "owrt: held lock should exit 4 (got $rc)"
    [ -d "$LOCK_DIR" ] || fail "owrt: a failed locker removed the owner's lock"
    echo 999999 > "$LOCK_DIR/pid"
    run apply uci:cfg01 || fail "owrt: stale lock not stolen"
    [ ! -e "$LOCK_DIR" ] || fail "owrt: lock left after apply"

    # alive: ALIVE_CMD override maps 0 -> 0, 1 -> 1, anything else -> 2.
    [ "$(ALIVE_CMD=true rc_of run alive)" = 0 ] || fail "owrt: alive (ALIVE_CMD ok)"
    [ "$(ALIVE_CMD=false rc_of run alive)" = 1 ] || fail "owrt: dead (ALIVE_CMD 1)"
    [ "$(ALIVE_CMD="sh -c 'exit 7'" rc_of run alive)" = 2 ] || fail "owrt: unknown (ALIVE_CMD 7)"
    # Default: `<init script> running`, then pidof as the second opinion.
    printf '#!/bin/sh\n[ "$1" = running ] && exit "${FAKE_RUNNING:-1}"\nexit 1\n' > "$tmp_root/owrt-init"
    chmod +x "$tmp_root/owrt-init"
    [ "$(INIT_SCRIPT="$tmp_root/owrt-init" FAKE_RUNNING=0 rc_of run alive)" = 0 ] ||
        fail "owrt: alive (init script running)"
    [ "$(INIT_SCRIPT="$tmp_root/owrt-init" FAKE_RUNNING=1 FAKE_PIDOF=0 rc_of run alive)" = 1 ] ||
        fail "owrt: dead (init script + pidof)"
    [ "$(INIT_SCRIPT="$tmp_root/owrt-init" FAKE_RUNNING=1 FAKE_PIDOF=1 rc_of run alive)" = 0 ] ||
        fail "owrt: alive (pidof fallback)"
}

# State left by 3.3.1: the whole server list in kpbr_server (scoped entries
# too), four mounts including /var/run/keen-pbr, a drop-in with the same line.
test_openwrt_upgrade_from_331() {
    owrt_setup
    cat > "$FAKE_UCI_DB" <<EOT
dhcp.cfg01=dnsmasq
dhcp.cfg01.kpbr_server=1.1.1.1
dhcp.cfg01.kpbr_server=/lan/192.168.1.1
dhcp.cfg01.addnmount=/usr/sbin/keen-pbr
dhcp.cfg01.addnmount=$CONFIG_DIR
dhcp.cfg01.addnmount=$CACHE_DIR
dhcp.cfg01.addnmount=$LEGACY_RUN_DIR
EOT
    mkdir -p "$confdir"
    printf '%s\n' "$want" > "$f1"
    touch -t 200001010000 "$f1"
    [ "$(run inspect | grep -cxF 'dropin_state=current')" = 1 ] || fail "owrt 3.3.1: the old drop-in should count as ours"
    reset_restarts

    run apply uci:cfg01 --default-upstreams=yes || fail "owrt 3.3.1: apply failed"
    [ "$(commits)" = 1 ] || fail "owrt 3.3.1: the first apply should commit once (got $(commits))"
    [ "$(restarts)" = 1 ] || fail "owrt 3.3.1: the first apply should restart once"
    grep -qxF "dhcp.cfg01.server=/lan/192.168.1.1" "$FAKE_UCI_DB" || fail "owrt 3.3.1: scoped server not returned"
    ! grep -qxF "dhcp.cfg01.kpbr_server=/lan/192.168.1.1" "$FAKE_UCI_DB" || fail "owrt 3.3.1: scoped server still stashed"
    grep -qxF "dhcp.cfg01.kpbr_server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt 3.3.1: unscoped server left the stash"
    ! grep -qxF "dhcp.cfg01.server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt 3.3.1: unscoped server in server"
    ! grep -q "$LEGACY_RUN_DIR" "$FAKE_UCI_DB" || fail "owrt 3.3.1: /var/run mount left"
    [ "$(grep -c '^dhcp.cfg01.addnmount=' "$FAKE_UCI_DB")" = 3 ] || fail "owrt 3.3.1: expected exactly three mounts"
    [ "$(touched "$f1")" = 0 ] || fail "owrt 3.3.1: the drop-in was rewritten"

    # Same upgrade, integration off after it: everything comes back.
    run remove uci:cfg01 || fail "owrt 3.3.1: remove failed"
    grep -qxF "dhcp.cfg01.server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt 3.3.1: server not restored"
    ! grep -q '^dhcp.cfg01.kpbr_server' "$FAKE_UCI_DB" || fail "owrt 3.3.1: stash left"

    # Upgrade state, no default upstreams: all of it returns, one commit.
    cat > "$FAKE_UCI_DB" <<EOT
dhcp.cfg01=dnsmasq
dhcp.cfg01.kpbr_server=1.1.1.1
dhcp.cfg01.kpbr_server=/lan/192.168.1.1
dhcp.cfg01.addnmount=$LEGACY_RUN_DIR
EOT
    : > "$tmp_root/uci.log"
    run apply uci:cfg01 --default-upstreams=no || fail "owrt 3.3.1: apply (no upstreams) failed"
    [ "$(commits)" = 1 ] || fail "owrt 3.3.1: apply (no upstreams) should commit once"
    grep -qxF "dhcp.cfg01.server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt 3.3.1: server not returned (no upstreams)"
    grep -qxF "dhcp.cfg01.server=/lan/192.168.1.1" "$FAKE_UCI_DB" || fail "owrt 3.3.1: scoped server not returned (no upstreams)"
}

test_openwrt_boot() {
    owrt_setup
    sh -n "$owrt_init" || fail "owrt boot: syntax error in the init script"
    grep -q '^START=18$' "$owrt_init" || fail "owrt boot: START must be 18 (dnsmasq is 19)"
    cat > "$FAKE_UCI_DB" <<EOT
dhcp.cfg01=dnsmasq
dhcp.cfg01.addnmount=/usr/sbin/keen-pbr
dhcp.cfg01.addnmount=$LEGACY_RUN_DIR
dhcp.guest=dnsmasq
dhcp.guest.confdir=$guestdir,*.conf
dhcp.guest.addnmount=$CONFIG_DIR
EOT
    reset_restarts
    cp "$FAKE_UCI_DB" "$tmp_root/uci.before"
    run boot || fail "owrt boot: failed"
    [ "$(cat "$f1")" = "$want" ] || fail "owrt boot: the drop-in was not rewritten"
    [ ! -e "$f2" ] || fail "owrt boot: wrote into a section without our mounts"
    [ -d "$CACHE_DIR" ] && [ -d "$LEGACY_RUN_DIR" ] || fail "owrt boot: mount targets missing"
    cmp -s "$FAKE_UCI_DB" "$tmp_root/uci.before" || fail "owrt boot: changed uci"
    [ "$(commits)" = 0 ] && [ "$(restarts)" = 0 ] || fail "owrt boot: committed or restarted"
    touch -t 200001010000 "$f1"
    run boot || fail "owrt boot: second run failed"
    [ "$(touched "$f1")" = 0 ] || fail "owrt boot: rewrote an unchanged drop-in"
    echo other > "$f1"
    run boot || fail "owrt boot: foreign run failed"
    [ "$(cat "$f1")" = other ] || fail "owrt boot: overwrote a foreign drop-in"
}

# Package removal: every section, no selector, never keen-pbr itself.
test_openwrt_uninstall() {
    owrt_setup
    cat > "$FAKE_UCI_DB" <<EOT
dhcp.cfg01=dnsmasq
dhcp.cfg01.addnmount=$CONFIG_DIR
dhcp.cfg01.addnmount=/some/user/mount
dhcp.cfg01.server=1.1.1.1
dhcp.cfg01.server=/lan/192.168.1.1
dhcp.guest=dnsmasq
dhcp.guest.confdir=$guestdir,*.conf
EOT
    run apply uci:cfg01 --default-upstreams=yes || fail "owrt uninstall: apply cfg01 failed"
    run apply uci:guest --default-upstreams=no || fail "owrt uninstall: apply guest failed"
    # A drop-in from 3.3.1 is ours too.
    printf '%s\n' "$want" > "$f2"
    reset_restarts; c=$(commits)
    # Pending dhcp changes and a held lock do not stop the removal.
    mkdir "$LOCK_DIR"; echo $$ > "$LOCK_DIR/pid"
    FAKE_UCI_CHANGES="dhcp.cfg01.foo=bar" UNINSTALL_LOCK_TRIES=1 run uninstall ||
        fail "owrt uninstall: failed"
    [ -d "$LOCK_DIR" ] || fail "owrt uninstall: removed somebody else's lock"
    rm -rf "$LOCK_DIR"
    [ ! -e "$f1" ] && [ ! -e "$f2" ] || fail "owrt uninstall: drop-ins left"
    grep -qxF "dhcp.cfg01.addnmount=$CONFIG_DIR" "$FAKE_UCI_DB" && fail "owrt uninstall: left $CONFIG_DIR mounted"
    grep -qxF "dhcp.cfg01.addnmount=/some/user/mount" "$FAKE_UCI_DB" || fail "owrt uninstall: removed a mount that is not ours"
    [ "$(grep -c '^dhcp.cfg01.addnmount=' "$FAKE_UCI_DB")" = 1 ] || fail "owrt uninstall: mounts left"
    ! grep -q 'addnmount=/usr/sbin/keen-pbr\|addnmount='"$CACHE_DIR" "$FAKE_UCI_DB" || fail "owrt uninstall: our mounts left"
    grep -qxF "dhcp.cfg01.server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt uninstall: server not returned"
    grep -qxF "dhcp.cfg01.server=/lan/192.168.1.1" "$FAKE_UCI_DB" || fail "owrt uninstall: scoped server lost"
    ! grep -q 'kpbr_server' "$FAKE_UCI_DB" || fail "owrt uninstall: stash left"
    [ "$(commits)" = "$((c + 1))" ] || fail "owrt uninstall: should commit once"
    [ "$(restarts)" = 1 ] || fail "owrt uninstall: should restart once"
    [ "$(tail -n 1 "$tmp_root/restart.log")" = restart ] || fail "owrt uninstall: should restart every instance"

    # A foreign drop-in stays; nothing left means no restart and no commit.
    mkdir -p "$(dirname "$f1")"
    printf 'conf-script=/usr/lib/keen-pbr/x\nserver=9.9.9.9\n' > "$f1"
    reset_restarts; c=$(commits)
    run uninstall || fail "owrt uninstall: second run failed"
    [ -f "$f1" ] || fail "owrt uninstall: removed a foreign drop-in"
    [ "$(restarts)" = 0 ] && [ "$(commits)" = "$c" ] || fail "owrt uninstall: idempotent run restarted/committed"

    # 3.3.1 state is cleaned too.
    rm -f "$f1"
    cat > "$FAKE_UCI_DB" <<EOT
dhcp.cfg01=dnsmasq
dhcp.cfg01.kpbr_server=1.1.1.1
dhcp.cfg01.addnmount=/usr/sbin/keen-pbr
dhcp.cfg01.addnmount=$LEGACY_RUN_DIR
EOT
    run uninstall || fail "owrt uninstall: 3.3.1 state failed"
    grep -qxF "dhcp.cfg01.server=1.1.1.1" "$FAKE_UCI_DB" || fail "owrt uninstall: 3.3.1 server not returned"
    ! grep -q 'addnmount' "$FAKE_UCI_DB" || fail "owrt uninstall: 3.3.1 mounts left"
}

# --- Keenetic ------------------------------------------------------------------

keen_setup() {
    kroot="$tmp_root/keen"
    rm -rf "$kroot"
    mkdir -p "$kroot"
    export DNSMASQ_CONF="$kroot/dnsmasq.conf" INIT_SCRIPT="$kroot/S56dnsmasq"
    export RECOMMENDED_CONF="$kroot/dnsmasq.recommended.conf"
    export FALLBACK_CONF="$kroot/fallback.conf"
    export DNSMASQ_SH=/opt/usr/lib/keen-pbr/dnsmasq.sh
    export LOCK_DIR="$tmp_root/hook.lock"
    backup="$DNSMASQ_CONF.backup-keenpbr"
    # Fake dnsmasq: `--test` fails when the file holds "bad-option".
    printf '#!/bin/sh\nfor a in "$@"; do case "$a" in --conf-file=*) f="${a#--conf-file=}" ;; esac; done\nif grep -q bad-option "$f"; then echo "dnsmasq: bad option at line 3 of $f" >&2; exit 1; fi\nexit 0\n' > "$kroot/dnsmasq-bin"
    chmod +x "$kroot/dnsmasq-bin"
    export DNSMASQ_BIN="$kroot/dnsmasq-bin"
    : > "$INIT_SCRIPT"
    printf 'server=8.8.8.8\nserver=8.8.4.4\n' > "$FALLBACK_CONF"
    run() { sh "$keen_hook" "$@"; }
    block_text='# BEGIN keen-pbr managed block
# Do not remove the following line, it is required for keen-pbr to function properly.
conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry
# END keen-pbr managed block'
    fb_block='# BEGIN keen-pbr fallback upstream (added on removal; replace or remove)
server=8.8.8.8
server=8.8.4.4
# END keen-pbr fallback upstream'
}

test_keenetic() {
    keen_setup

    # No dnsmasq installed.
    rm -f "$INIT_SCRIPT"
    reset_restarts
    rc=0
    err=$(run apply entware:default 2>&1 >/dev/null) || rc=$?
    [ "$rc" = 2 ] || fail "keenetic: apply without dnsmasq should exit 2 (got $rc)"
    [ "$err" = "dnsmasq is not installed (opkg install dnsmasq-full)" ] || fail "keenetic: wrong message: $err"
    [ "$(restarts)" = 0 ] || fail "keenetic: restarted without dnsmasq"
    : > "$INIT_SCRIPT"

    printf 'user=nobody\nserver=1.1.1.1\n' > "$DNSMASQ_CONF"
    [ "$(run status)" = not-installed ] || fail "keenetic: status before apply"
    [ "$(rc_of run apply)" = 64 ] || fail "keenetic: apply without selector should exit 64"
    [ "$(rc_of run apply uci:x)" = 3 ] || fail "keenetic: foreign selector should exit 3"
    [ "$(rc_of run remove uci:x)" = 3 ] || fail "keenetic: remove foreign selector should exit 3"
    out=$(run inspect) || fail "keenetic: inspect failed"
    printf '%s\n' "$out" | grep -qxF 'hook_version=2' || fail "keenetic: inspect hook_version"
    printf '%s\n' "$out" | grep -qxF 'selector=entware:default' || fail "keenetic: inspect selector"
    printf '%s\n' "$out" | grep -qxF 'dropin_include=managed' || fail "keenetic: inspect include"
    printf '%s\n' "$out" | grep -qxF 'dropin_state=absent' || fail "keenetic: inspect state before apply"
    [ "$(restarts)" = 0 ] || fail "keenetic: rejected calls restarted"

    # First apply appends the 3.3.1 block once and restarts.
    run apply entware:default --default-upstreams=yes || fail "keenetic: apply failed"
    [ "$(grep -c 'BEGIN keen-pbr managed block' "$DNSMASQ_CONF")" = 1 ] || fail "keenetic: block count"
    grep -qxF "$block_text" "$DNSMASQ_CONF" || true
    [ "$(grep -v '^$' "$DNSMASQ_CONF" | tail -n 4)" = "$block_text" ] || fail "keenetic: block text differs from 3.3.1"
    grep -qxF 'server=1.1.1.1' "$DNSMASQ_CONF" || fail "keenetic: user config lost"
    [ "$(restarts)" = 1 ] || fail "keenetic: apply should restart once"
    [ "$(run status)" = installed ] || fail "keenetic: status after apply"
    run inspect | grep -qxF 'dropin_state=current' || fail "keenetic: inspect state after apply"
    [ ! -e "$LOCK_DIR" ] || fail "keenetic: lock left behind"
    [ ! -e "$kroot/tmp" ] || fail "keenetic: apply created a tmp directory"

    # Second apply writes nothing.
    touch -t 200001010000 "$DNSMASQ_CONF"
    run apply entware:default || fail "keenetic: second apply failed"
    [ "$(touched "$DNSMASQ_CONF")" = 0 ] || fail "keenetic: second apply rewrote dnsmasq.conf"
    [ "$(restarts)" = 2 ] || fail "keenetic: second apply should restart again"

    # A 3.3.1 install already has the block (the old template): untouched.
    { printf 'user=nobody\n\n'; printf '%s\n' "$block_text"; } > "$DNSMASQ_CONF"
    touch -t 200001010000 "$DNSMASQ_CONF"
    run apply entware:default || fail "keenetic: apply on a 3.3.1 config failed"
    [ "$(touched "$DNSMASQ_CONF")" = 0 ] || fail "keenetic: apply rewrote a 3.3.1 dnsmasq.conf"

    # A damaged block is not duplicated.
    printf 'user=nobody\n# BEGIN keen-pbr managed block\nfoo\n' > "$DNSMASQ_CONF"
    [ "$(rc_of run apply entware:default)" = 1 ] || fail "keenetic: damaged block should exit 1"
    [ "$(grep -c 'BEGIN keen-pbr managed block' "$DNSMASQ_CONF")" = 1 ] || fail "keenetic: damaged block duplicated"

    # Remove (integration off): no file changes, one restart.
    { printf 'user=nobody\n\n'; printf '%s\n' "$block_text"; } > "$DNSMASQ_CONF"
    cp "$DNSMASQ_CONF" "$kroot/conf.before"
    touch -t 200001010000 "$DNSMASQ_CONF"
    reset_restarts
    run remove entware:default || fail "keenetic: remove failed"
    cmp -s "$DNSMASQ_CONF" "$kroot/conf.before" || fail "keenetic: remove changed dnsmasq.conf"
    [ "$(touched "$DNSMASQ_CONF")" = 0 ] || fail "keenetic: remove rewrote dnsmasq.conf"
    [ "$(restarts)" = 1 ] || fail "keenetic: remove should restart once"
    [ "$(run status)" = installed ] || fail "keenetic: remove must keep the block"

    # alive: Entware's rc.func `check` prints "alive."/"dead."; unknown without
    # the init script.
    alive_init="$kroot/S56alive"
    printf '#!/bin/sh\n[ "$1" = check ] || exit 64\ncase "${FAKE_STATE:-}" in\n  alive) echo " Checking dnsmasq... alive."; exit 0 ;;\n  dead) echo " Checking dnsmasq... dead."; exit 1 ;;\n  *) exit "${FAKE_RC:-5}" ;;\nesac\n' > "$alive_init"
    chmod +x "$alive_init"
    unset ALIVE_CMD
    [ "$(INIT_SCRIPT="$alive_init" FAKE_STATE=alive rc_of run alive)" = 0 ] || fail "keenetic: alive"
    [ "$(INIT_SCRIPT="$alive_init" FAKE_STATE=dead rc_of run alive)" = 1 ] || fail "keenetic: dead"
    [ "$(INIT_SCRIPT="$alive_init" FAKE_STATE= FAKE_RC=5 rc_of run alive)" = 2 ] || fail "keenetic: unknown (odd output)"
    [ "$(INIT_SCRIPT="$kroot/missing" rc_of run alive)" = 2 ] || fail "keenetic: unknown (no init script)"
    [ "$(INIT_SCRIPT="$alive_init" ALIVE_CMD=false rc_of run alive)" = 1 ] || fail "keenetic: dead (ALIVE_CMD)"
    # A bare pidof must not be what decides (firmware dnsmasq).
    [ "$(PATH="$fake_bin:$PATH" FAKE_PIDOF=1 INIT_SCRIPT="$alive_init" FAKE_STATE=dead rc_of run alive)" = 1 ] ||
        fail "keenetic: pidof must not override the init script"
}

test_keenetic_uninstall() {
    keen_setup

    # dnsmasq has no-resolv and nothing else gives it an upstream: the block
    # is replaced by the fallback servers, in place.
    printf 'user=nobody\nno-resolv\nserver=/lan/192.168.1.1\n\n%s\nlog-async\n' "$block_text" > "$DNSMASQ_CONF"
    reset_restarts
    mkdir "$LOCK_DIR"; echo $$ > "$LOCK_DIR/pid"
    UNINSTALL_LOCK_TRIES=1 run uninstall || fail "keenetic uninstall: failed"
    [ -d "$LOCK_DIR" ] || fail "keenetic uninstall: removed somebody else's lock"
    rm -rf "$LOCK_DIR"
    expected=$(printf 'user=nobody\nno-resolv\nserver=/lan/192.168.1.1\n\n%s\nlog-async\n' "$fb_block")
    [ "$(cat "$DNSMASQ_CONF")" = "$expected" ] || fail "keenetic uninstall: expected the fallback block in place of the managed block"
    ! grep -q 'managed block' "$DNSMASQ_CONF" || fail "keenetic uninstall: managed block left"
    [ "$(restarts)" = 1 ] || fail "keenetic uninstall: should restart once"
    # Nothing to remove the second time.
    reset_restarts
    run uninstall || fail "keenetic uninstall: second run failed"
    [ "$(restarts)" = 0 ] || fail "keenetic uninstall: second run restarted"

    # Without no-resolv the block is just removed.
    printf 'user=nobody\n\n%s\nlog-async\n' "$block_text" > "$DNSMASQ_CONF"
    run uninstall || fail "keenetic uninstall: run (no no-resolv) failed"
    ! grep -q 'keen-pbr' "$DNSMASQ_CONF" || fail "keenetic uninstall: no-resolv absent but the block was replaced"
    grep -qxF 'log-async' "$DNSMASQ_CONF" || fail "keenetic uninstall: user config lost"

    # no-resolv with an upstream of its own: just removed.
    for own in 'server=1.1.1.1' 'servers-file=/opt/etc/servers'; do
        printf 'no-resolv\n%s\n\n%s\n' "$own" "$block_text" > "$DNSMASQ_CONF"
        run uninstall || fail "keenetic uninstall: run ($own) failed"
        ! grep -q 'keen-pbr' "$DNSMASQ_CONF" || fail "keenetic uninstall: fallback added although dnsmasq has $own"
        grep -qxF "$own" "$DNSMASQ_CONF" || fail "keenetic uninstall: lost $own"
    done

    # No dnsmasq-fallback.conf: the built-in servers.
    printf 'no-resolv\n\n%s\n' "$block_text" > "$DNSMASQ_CONF"
    FALLBACK_CONF=/nonexistent run uninstall || fail "keenetic uninstall: run (no fallback file) failed"
    grep -qxF 'server=8.8.8.8' "$DNSMASQ_CONF" || fail "keenetic uninstall: built-in fallback missing"

    # A dnsmasq.conf keen-pbr never changed is left alone.
    printf 'no-resolv\nuser=nobody\n' > "$DNSMASQ_CONF"
    cp "$DNSMASQ_CONF" "$kroot/conf.before"
    reset_restarts
    run uninstall || fail "keenetic uninstall: untouched run failed"
    cmp -s "$DNSMASQ_CONF" "$kroot/conf.before" || fail "keenetic uninstall: changed a config without the block"
    [ "$(restarts)" = 0 ] || fail "keenetic uninstall: restarted without a change"

    # The fallback block left behind does not stop a later install.
    printf 'no-resolv\n\n%s\n' "$fb_block" > "$DNSMASQ_CONF"
    run apply entware:default || fail "keenetic uninstall: reinstall failed"
    grep -q 'BEGIN keen-pbr managed block' "$DNSMASQ_CONF" || fail "keenetic uninstall: reinstall did not add the block"
}

test_keenetic_recommended() {
    keen_setup
    cp "$keen_recommended" "$RECOMMENDED_CONF"
    # The shipped file carries the managed block and no ipset-era cache TTL.
    grep -qxF 'conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry' "$keen_recommended" ||
        fail "keenetic recommended: managed block missing"
    ! grep -q 'max-cache-ttl\|ipset' "$keen_recommended" || fail "keenetic recommended: ipset-era settings left"
    grep -q '^no-resolv' "$keen_recommended" || fail "keenetic recommended: no-resolv missing"
    grep -qxF 'interface-name=my.keenetic.net,ezcfg0' "$keen_recommended" || fail "keenetic recommended: my.keenetic.net record missing"

    # dnsmasq not installed.
    rm -f "$INIT_SCRIPT"
    rc=0; run install-recommended-config >/dev/null 2>&1 || rc=$?
    [ "$rc" = 2 ] || fail "keenetic recommended: missing dnsmasq should exit 2 (got $rc)"
    : > "$INIT_SCRIPT"

    # Identical: nothing is written.
    cp "$RECOMMENDED_CONF" "$DNSMASQ_CONF"
    touch -t 200001010000 "$DNSMASQ_CONF"
    reset_restarts
    out=$(run install-recommended-config) || fail "keenetic recommended: identical run failed"
    case "$out" in *"nothing changed"*) ;; *) fail "keenetic recommended: identical run should say so: $out" ;; esac
    [ "$(touched "$DNSMASQ_CONF")" = 0 ] || fail "keenetic recommended: rewrote an identical file"
    [ ! -e "$backup" ] || fail "keenetic recommended: made a backup of an identical file"
    [ "$(restarts)" = 0 ] || fail "keenetic recommended: restarted for an identical file"

    # dnsmasq --test fails: everything stays, the output is shown.
    printf 'user=nobody\nserver=1.1.1.1\n' > "$DNSMASQ_CONF"
    cp "$DNSMASQ_CONF" "$kroot/conf.before"
    { cat "$keen_recommended"; echo bad-option; } > "$RECOMMENDED_CONF"
    rc=0; out=$(run install-recommended-config 2>&1) || rc=$?
    [ "$rc" != 0 ] || fail "keenetic recommended: a rejected file should fail"
    case "$out" in *"bad option at line 3"*) ;; *) fail "keenetic recommended: dnsmasq's output not shown: $out" ;; esac
    cmp -s "$DNSMASQ_CONF" "$kroot/conf.before" || fail "keenetic recommended: changed dnsmasq.conf after a failed test"
    [ ! -e "$backup" ] || fail "keenetic recommended: made a backup after a failed test"
    [ "$(restarts)" = 0 ] || fail "keenetic recommended: restarted after a failed test"
    [ -z "$(ls "$kroot" | grep 'keen-pbr-new')" ] || fail "keenetic recommended: temp file left"

    # Success: backup of the old file, new file, one restart.
    cp "$keen_recommended" "$RECOMMENDED_CONF"
    out=$(run install-recommended-config) || fail "keenetic recommended: install failed"
    cmp -s "$DNSMASQ_CONF" "$RECOMMENDED_CONF" || fail "keenetic recommended: dnsmasq.conf is not the recommended file"
    cmp -s "$backup" "$kroot/conf.before" || fail "keenetic recommended: the backup is not the old file"
    [ "$(restarts)" = 1 ] || fail "keenetic recommended: install should restart once"
    case "$out" in *"$backup"*) ;; *) fail "keenetic recommended: the output should name the backup: $out" ;; esac
    [ "$(run status)" = installed ] || fail "keenetic recommended: the block is missing after install"

    # The backup is overwritten by the next replacement.
    printf 'user=other\n' > "$DNSMASQ_CONF"
    run install-recommended-config >/dev/null || fail "keenetic recommended: second install failed"
    [ "$(cat "$backup")" = "user=other" ] || fail "keenetic recommended: the backup was not overwritten"

    # Running it again changes nothing.
    touch -t 200001010000 "$DNSMASQ_CONF" "$backup"
    run install-recommended-config >/dev/null || fail "keenetic recommended: third run failed"
    [ "$(touched "$DNSMASQ_CONF" "$backup")" = 0 ] || fail "keenetic recommended: third run wrote files"

    # apply on the recommended config writes nothing.
    touch -t 200001010000 "$DNSMASQ_CONF"
    run apply entware:default || fail "keenetic recommended: apply failed"
    [ "$(touched "$DNSMASQ_CONF")" = 0 ] || fail "keenetic recommended: apply rewrote the recommended config"
}

test_keenetic_entry_script() {
    keen_setup
    fb='server=8.8.8.8
server=8.8.4.4'
    printf '#!/bin/sh\ncase "${FAKE_BIN:-ok}" in\n  ok) echo "server=/example.org/10.0.0.1"; echo "# stamp" ;;\n  fail) echo "partial"; exit 1 ;;\nesac\n' > "$kroot/keen-pbr"
    chmod +x "$kroot/keen-pbr"
    entry() {
        KEEN_PBR_BIN="${BIN:-$kroot/keen-pbr}" sh "$keen_sh" dnsmasq-config-entry 2>/dev/null
    }
    [ "$(entry)" = 'server=/example.org/10.0.0.1
# stamp' ] || fail "keenetic entry: should print only keen-pbr's output"
    [ "$(FAKE_BIN=fail entry)" = "partial
$fb" ] || fail "keenetic entry: a failing binary should be followed by the fallback servers"
    [ "$(BIN=$kroot/missing entry)" = "$fb" ] || fail "keenetic entry: a missing binary should print the fallback servers"
    [ -z "$(sh "$keen_sh" activate; sh "$keen_sh" reload; sh "$keen_sh" prepare)" ] || fail "keenetic entry: legacy commands should print nothing"
    [ "$(rc_of sh "$keen_sh" bogus)" = 1 ] || fail "keenetic entry: unknown command should exit 1"
}

# --- Debian --------------------------------------------------------------------

deb_setup() {
    droot="$tmp_root/deb"
    rm -rf "$droot"
    mkdir -p "$droot/etc/dnsmasq.d" "$droot/bin"
    export DNSMASQ_CONF_DIR="$droot/etc/dnsmasq.d" DEFAULT_CONF="$droot/etc/dnsmasq.conf"
    export KEEN_CONF_DIR="$droot/etc/keen-pbr/dnsmasq.d"
    unset LEGACY_CONF
    printf 'user=dnsmasq\n' > "$DEFAULT_CONF"
    export RECOMMENDED_CONF="$droot/recommended.conf" FALLBACK_CONF="$droot/fallback.conf"
    export LOCK_DIR="$tmp_root/hook.lock"
    f="$KEEN_CONF_DIR/keen-pbr.conf"
    legacy="$DNSMASQ_CONF_DIR/keen-pbr.conf"
    block="# BEGIN keen-pbr managed block
conf-dir=$KEEN_CONF_DIR/,*.conf
# END keen-pbr managed block"
    want='conf-script=/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry'
    backup="$DEFAULT_CONF.backup-keenpbr"
    printf 'server=8.8.8.8\nserver=8.8.4.4\n' > "$FALLBACK_CONF"
    printf '#!/bin/sh\nfor a in "$@"; do case "$a" in --conf-file=*) f="${a#--conf-file=}" ;; esac; done\nif grep -q bad-option "$f"; then echo "dnsmasq: bad option at line 3 of $f" >&2; exit 1; fi\nexit 0\n' > "$droot/bin/dnsmasq"
    chmod +x "$droot/bin/dnsmasq"
    export DNSMASQ_BIN="$droot/bin/dnsmasq"
    run() { PATH="$droot/bin:$PATH" sh "$deb_hook" "$@"; }
    fb_body='# BEGIN keen-pbr fallback upstream (added on removal; replace or remove)
server=8.8.8.8
server=8.8.4.4
# END keen-pbr fallback upstream'
}

test_debian() {
    deb_setup
    [ "$(rc_of run apply)" = 64 ] || fail "debian: apply without selector should exit 64"
    [ "$(rc_of run apply systemd:other.service)" = 3 ] || fail "debian: other unit should exit 3"
    [ "$(rc_of run remove systemd:other.service)" = 3 ] || fail "debian: remove other unit should exit 3"
    printf '#!/bin/sh\ncase "$1" in\n  show) echo 1234 ;;\n  is-enabled) exit 0 ;;\n  *) exit 9 ;;\nesac\n' > "$droot/bin/systemctl"
    chmod +x "$droot/bin/systemctl"
    out=$(SYSTEMCTL="$droot/bin/systemctl" run inspect) || fail "debian: inspect failed"
    printf '%s\n' "$out" | grep -qxF 'hook_version=2' || fail "debian: inspect hook_version"
    printf '%s\n' "$out" | grep -qxF 'selector=systemd:dnsmasq.service' || fail "debian: inspect selector"
    printf '%s\n' "$out" | grep -qxF 'main_pid=1234' || fail "debian: inspect main_pid"
    printf '%s\n' "$out" | grep -qxF 'dropin_include=managed' || fail "debian: inspect include"
    printf '%s\n' "$out" | grep -qxF "dropin_dir=$KEEN_CONF_DIR" || fail "debian: inspect folder"
    printf '%s\n' "$out" | grep -qxF "dropin_file=$f" || fail "debian: inspect drop-in name"
    printf '%s\n' "$out" | grep -qxF "dropin_content=$want" || fail "debian: inspect drop-in content"
    printf '%s\n' "$out" | grep -qxF 'dropin_state=absent' || fail "debian: inspect state before apply"
    rm -f "$droot/bin/systemctl"

    reset_restarts
    run apply systemd:dnsmasq.service --default-upstreams=yes || fail "debian: apply failed"
    [ "$(cat "$f")" = "$want" ] || fail "debian: conf file wrong"
    [ "$(sed -n '2,$p' "$DEFAULT_CONF" | grep -c '^# BEGIN keen-pbr managed block')" = 1 ] || fail "debian: block not added once"
    grep -qxF "conf-dir=$KEEN_CONF_DIR/,*.conf" "$DEFAULT_CONF" || fail "debian: conf-dir line missing"
    [ "$(restarts)" = 1 ] || fail "debian: apply should restart once"
    [ "$(run status)" = installed ] || fail "debian: status"
    run inspect | grep -qxF 'dropin_state=current' || fail "debian: inspect state after apply"

    # Second apply writes nothing (no second block either); the restart still happens.
    touch -t 200001010000 "$f" "$DEFAULT_CONF"
    run apply systemd:dnsmasq.service || fail "debian: second apply failed"
    [ "$(touched "$f" "$DEFAULT_CONF")" = 0 ] || fail "debian: second apply rewrote a file"
    [ "$(grep -c '^# BEGIN keen-pbr managed block' "$DEFAULT_CONF")" = 1 ] || fail "debian: second apply added another block"
    [ "$(restarts)" = 2 ] || fail "debian: second apply should restart again"

    # Remove (integration off): no file changes, one restart.
    touch -t 200001010000 "$f" "$DEFAULT_CONF"
    run remove systemd:dnsmasq.service || fail "debian: remove failed"
    [ "$(cat "$f")" = "$want" ] || fail "debian: remove changed the entry"
    [ "$(touched "$f" "$DEFAULT_CONF")" = 0 ] || fail "debian: remove rewrote a file"
    [ "$(restarts)" = 3 ] || fail "debian: remove should restart once"

    # Foreign file under our name is not overwritten.
    echo other > "$f"
    [ "$(rc_of run apply systemd:dnsmasq.service)" = 1 ] || fail "debian: foreign entry should exit 1"
    [ "$(cat "$f")" = other ] || fail "debian: foreign entry overwritten"
    run inspect | grep -qxF 'dropin_state=foreign' || fail "debian: inspect should report foreign"
    rm -f "$f"

    # A damaged block (no conf-dir line) is not papered over.
    printf '# BEGIN keen-pbr managed block\n# END keen-pbr managed block\n' > "$DEFAULT_CONF"
    [ "$(rc_of run apply systemd:dnsmasq.service)" = 1 ] || fail "debian: damaged block should exit 1"
    printf 'user=dnsmasq\n' > "$DEFAULT_CONF"

    # 3.3.1 entry: removed when it holds only keen-pbr lines.
    rm -rf "$KEEN_CONF_DIR"
    printf '%s\n' "$want" > "$legacy"
    run inspect | grep -qxF 'dropin_state=absent' || fail "debian: legacy entry should need apply"
    run apply systemd:dnsmasq.service || fail "debian: apply with legacy entry failed"
    [ ! -e "$legacy" ] || fail "debian: legacy entry not removed"
    [ "$(cat "$f")" = "$want" ] || fail "debian: entry missing after migration"
    # ...but a legacy file with foreign content stays and is reported.
    printf '%s\nserver=1.1.1.1\n' "$want" > "$legacy"
    [ "$(rc_of run apply systemd:dnsmasq.service)" = 1 ] || fail "debian: foreign legacy file should exit 1"
    [ "$(wc -l < "$legacy")" = 2 ] || fail "debian: foreign legacy file changed"
    run inspect | grep -qxF 'dropin_state=foreign' || fail "debian: inspect should report foreign legacy"
    rm -f "$legacy"

    # alive: 0 active, 1 inactive/failed, 2 unknown.
    [ "$(ALIVE_CMD=true rc_of run alive)" = 0 ] || fail "debian: alive (ALIVE_CMD ok)"
    [ "$(ALIVE_CMD=false rc_of run alive)" = 1 ] || fail "debian: dead (ALIVE_CMD 1)"
    [ "$(ALIVE_CMD="sh -c 'exit 7'" rc_of run alive)" = 2 ] || fail "debian: unknown (ALIVE_CMD 7)"
    printf '#!/bin/sh\n[ "$1 $2 $3" = "is-active --quiet dnsmasq" ] && exit "${FAKE_ACTIVE:-3}"\nexit 9\n' > "$droot/bin/systemctl"
    chmod +x "$droot/bin/systemctl"
    [ "$(FAKE_ACTIVE=0 rc_of run alive)" = 0 ] || fail "debian: alive (systemctl active)"
    [ "$(FAKE_ACTIVE=3 rc_of run alive)" = 1 ] || fail "debian: dead (systemctl inactive)"
    rm -f "$droot/bin/systemctl"
}

test_debian_missing_dnsmasq() {
    deb_setup
    rm -f "$droot/bin/dnsmasq"
    unset DNSMASQ_BIN
    # Only meaningful when the host has no dnsmasq of its own.
    command -v dnsmasq >/dev/null 2>&1 && return 0
    reset_restarts
    rc=0; run apply systemd:dnsmasq.service >/dev/null 2>&1 || rc=$?
    [ "$rc" = 2 ] || fail "debian: apply without dnsmasq should exit 2 (got $rc)"
    rc=0; run install-recommended-config >/dev/null 2>&1 || rc=$?
    [ "$rc" = 2 ] || fail "debian: install-recommended-config without dnsmasq should exit 2 (got $rc)"
    [ "$(restarts)" = 0 ] || fail "debian: restarted without dnsmasq"
}

test_debian_uninstall() {
    deb_setup
    printf 'user=dnsmasq\n' > "$DEFAULT_CONF"
    run apply systemd:dnsmasq.service || fail "debian uninstall: apply failed"

    # dnsmasq.conf uses resolv.conf: block, entry and folder go away.
    reset_restarts
    mkdir "$LOCK_DIR"; echo $$ > "$LOCK_DIR/pid"
    UNINSTALL_LOCK_TRIES=1 run uninstall || fail "debian uninstall: failed"
    [ -d "$LOCK_DIR" ] || fail "debian uninstall: removed somebody else's lock"
    rm -rf "$LOCK_DIR"
    [ ! -e "$f" ] && [ ! -d "$KEEN_CONF_DIR" ] || fail "debian uninstall: entry or folder left"
    ! grep -q 'keen-pbr' "$DEFAULT_CONF" || fail "debian uninstall: block left: $(cat "$DEFAULT_CONF")"
    grep -qx 'user=dnsmasq' "$DEFAULT_CONF" || fail "debian uninstall: user lines lost"
    [ "$(restarts)" = 1 ] || fail "debian uninstall: should restart once"
    reset_restarts
    touch -t 200001010000 "$DEFAULT_CONF"
    run uninstall || fail "debian uninstall: second run failed"
    [ "$(restarts)" = 0 ] || fail "debian uninstall: second run restarted"
    [ "$(touched "$DEFAULT_CONF")" = 0 ] || fail "debian uninstall: second run wrote dnsmasq.conf"

    # no-resolv and no upstream: the fallback servers stay in a marked block.
    printf 'no-resolv\nserver=/lan/192.168.1.1\n' > "$DEFAULT_CONF"
    run apply systemd:dnsmasq.service || fail "debian uninstall: apply (no-resolv) failed"
    reset_restarts
    run uninstall || fail "debian uninstall: run (no-resolv) failed"
    [ ! -e "$f" ] || fail "debian uninstall: entry left (no-resolv)"
    grep -qxF "$(printf '%s' "$fb_body" | sed -n 1p)" "$DEFAULT_CONF" || fail "debian uninstall: fallback block missing: $(cat "$DEFAULT_CONF")"
    [ "$(sed -n '/^# BEGIN keen-pbr fallback/,/^# END keen-pbr fallback/p' "$DEFAULT_CONF")" = "$fb_body" ] || fail "debian uninstall: wrong fallback block"
    ! grep -q 'managed block' "$DEFAULT_CONF" || fail "debian uninstall: managed block left (no-resolv)"
    [ "$(restarts)" = 1 ] || fail "debian uninstall: no-resolv case should restart once"
    reset_restarts
    run uninstall || fail "debian uninstall: repeat (no-resolv) failed"
    [ "$(grep -c 'BEGIN keen-pbr fallback' "$DEFAULT_CONF")" = 1 ] && [ "$(restarts)" = 0 ] || fail "debian uninstall: repeat changed something"
    # A reinstall adds the managed block again.
    run apply systemd:dnsmasq.service || fail "debian uninstall: reinstall failed"
    [ "$(cat "$f")" = "$want" ] || fail "debian uninstall: reinstall did not restore the entry"
    grep -q 'managed block' "$DEFAULT_CONF" || fail "debian uninstall: reinstall did not add the block"

    # no-resolv with an upstream in dnsmasq.conf or another drop-in: nothing added.
    printf 'no-resolv\nserver=1.1.1.1\n' > "$DEFAULT_CONF"
    run apply systemd:dnsmasq.service || fail "debian uninstall: apply (own upstream) failed"
    run uninstall || fail "debian uninstall: run (own upstream) failed"
    ! grep -q 'keen-pbr' "$DEFAULT_CONF" || fail "debian uninstall: added fallback although dnsmasq has an upstream"
    printf 'no-resolv\n' > "$DEFAULT_CONF"
    printf 'server=9.9.9.9\n' > "$DNSMASQ_CONF_DIR/user.conf"
    run apply systemd:dnsmasq.service || fail "debian uninstall: apply (user drop-in) failed"
    run uninstall || fail "debian uninstall: run (user drop-in) failed"
    ! grep -q 'keen-pbr' "$DEFAULT_CONF" || fail "debian uninstall: added fallback next to a user upstream"
    [ -f "$DNSMASQ_CONF_DIR/user.conf" ] || fail "debian uninstall: removed a user file"
    rm -f "$DNSMASQ_CONF_DIR/user.conf"

    # The 3.3.1 entry with no-resolv: removed, fallback servers added at the end.
    printf 'no-resolv\n' > "$DEFAULT_CONF"
    rm -rf "$KEEN_CONF_DIR"
    printf '%s\n' "$want" > "$legacy"
    run uninstall || fail "debian uninstall: legacy run failed"
    [ ! -e "$legacy" ] || fail "debian uninstall: legacy entry left"
    grep -qxF 'server=8.8.8.8' "$DEFAULT_CONF" || fail "debian uninstall: legacy case lacks fallback servers"
    # A foreign legacy file stays.
    printf 'user=dnsmasq\n' > "$DEFAULT_CONF"
    echo 'server=1.1.1.1' > "$legacy"
    reset_restarts
    run uninstall || fail "debian uninstall: foreign run failed"
    [ -f "$legacy" ] && [ "$(restarts)" = 0 ] || fail "debian uninstall: touched a foreign legacy file"
    rm -f "$legacy"

    # A foreign file under our name stays, with its folder.
    mkdir -p "$KEEN_CONF_DIR"; echo 'server=1.1.1.1' > "$f"
    run uninstall || fail "debian uninstall: foreign entry run failed"
    [ -f "$f" ] || fail "debian uninstall: removed a foreign entry"
    rm -rf "$KEEN_CONF_DIR"

    # An empty dnsmasq-fallback.conf: the built-in servers keep DNS working.
    printf 'no-resolv\n' > "$DEFAULT_CONF"
    : > "$FALLBACK_CONF"
    run apply systemd:dnsmasq.service || fail "debian uninstall: apply (empty fallback) failed"
    run uninstall || fail "debian uninstall: run (empty fallback) failed"
    grep -qxF 'server=8.8.8.8' "$DEFAULT_CONF" || fail "debian uninstall: built-in fallback missing"
}

test_debian_recommended() {
    deb_setup
    cp "$deb_recommended" "$RECOMMENDED_CONF"
    ! grep -q 'max-cache-ttl\|ipset' "$deb_recommended" || fail "debian recommended: ipset-era settings left"
    grep -q '^no-resolv' "$deb_recommended" || fail "debian recommended: no-resolv missing"
    grep -qxF '# BEGIN keen-pbr managed block' "$deb_recommended" &&
        grep -qxF 'conf-dir=/etc/keen-pbr/dnsmasq.d/,*.conf' "$deb_recommended" &&
        grep -qxF '# END keen-pbr managed block' "$deb_recommended" || fail "debian recommended: the managed block is missing"

    # Identical file and the entry already there: nothing is written.
    sed "s#/etc/keen-pbr/dnsmasq.d#$KEEN_CONF_DIR#" "$deb_recommended" > "$RECOMMENDED_CONF"
    cp "$RECOMMENDED_CONF" "$DEFAULT_CONF"
    run apply systemd:dnsmasq.service || fail "debian recommended: apply failed"
    cmp -s "$DEFAULT_CONF" "$RECOMMENDED_CONF" || fail "debian recommended: apply changed the recommended config"
    touch -t 200001010000 "$DEFAULT_CONF" "$f"
    reset_restarts
    out=$(run install-recommended-config) || fail "debian recommended: identical run failed"
    case "$out" in *"nothing changed"*) ;; *) fail "debian recommended: identical run should say so: $out" ;; esac
    [ "$(touched "$DEFAULT_CONF" "$f")" = 0 ] || fail "debian recommended: wrote files for an identical config"
    [ ! -e "$backup" ] && [ "$(restarts)" = 0 ] || fail "debian recommended: backup or restart for an identical config"
    rm -rf "$KEEN_CONF_DIR"

    # dnsmasq --test fails: everything stays, including the missing folder.
    printf 'user=dnsmasq\nserver=1.1.1.1\n' > "$DEFAULT_CONF"
    cp "$DEFAULT_CONF" "$droot/conf.before"
    { cat "$RECOMMENDED_CONF"; echo bad-option; } > "$droot/rec.bad"
    cp "$droot/rec.bad" "$RECOMMENDED_CONF"
    rc=0; out=$(run install-recommended-config 2>&1) || rc=$?
    [ "$rc" != 0 ] || fail "debian recommended: a rejected file should fail"
    case "$out" in *"bad option at line"*) ;; *) fail "debian recommended: dnsmasq's output not shown: $out" ;; esac
    cmp -s "$DEFAULT_CONF" "$droot/conf.before" || fail "debian recommended: changed dnsmasq.conf after a failed test"
    [ ! -e "$backup" ] && [ ! -e "$f" ] && [ ! -d "$KEEN_CONF_DIR" ] && [ "$(restarts)" = 0 ] || fail "debian recommended: side effects after a failed test"

    # Success: the folder file exists during the test, backup, new file, one restart.
    sed "s#/etc/keen-pbr/dnsmasq.d#$KEEN_CONF_DIR#" "$deb_recommended" > "$RECOMMENDED_CONF"
    printf '#!/bin/sh\nfor a in "$@"; do case "$a" in --conf-file=*) c="${a#--conf-file=}" ;; esac; done\nd=$(sed -n "s/^conf-dir=\\(.*\\)\\/,\\*.conf$/\\1/p" "$c")\n[ -f "$d/keen-pbr.conf" ] || { echo "dnsmasq: no entry in $d" >&2; exit 1; }\nexit 0\n' > "$droot/bin/dnsmasq"
    out=$(run install-recommended-config) || fail "debian recommended: install failed"
    cmp -s "$DEFAULT_CONF" "$RECOMMENDED_CONF" || fail "debian recommended: dnsmasq.conf is not the recommended file"
    cmp -s "$backup" "$droot/conf.before" || fail "debian recommended: the backup is not the old file"
    [ "$(cat "$f")" = "$want" ] || fail "debian recommended: the entry file was not created"
    grep -qxF "conf-dir=$KEEN_CONF_DIR/,*.conf" "$DEFAULT_CONF" || fail "debian recommended: the block is missing from dnsmasq.conf"
    [ "$(restarts)" = 1 ] || fail "debian recommended: install should restart once"
    case "$out" in *"$backup"*) ;; *) fail "debian recommended: the output should name the backup: $out" ;; esac

    # Identical file but no entry file: only the entry is created.
    rm -rf "$KEEN_CONF_DIR"; rm -f "$backup"
    run install-recommended-config >/dev/null || fail "debian recommended: entry-only run failed"
    [ "$(cat "$f")" = "$want" ] || fail "debian recommended: the entry was not recreated"
    [ ! -e "$backup" ] || fail "debian recommended: backup for an unchanged config"
}

test_debian_entry_script() {
    deb_setup
    fb='server=8.8.8.8
server=8.8.4.4'
    printf '#!/bin/sh\ncase "${FAKE_BIN:-ok}" in\n  ok) echo "server=/example.org/10.0.0.1" ;;\n  fail) echo "partial"; exit 1 ;;\nesac\n' > "$droot/keen-pbr"
    chmod +x "$droot/keen-pbr"
    entry() {
        KEEN_PBR_BIN="${BIN:-$droot/keen-pbr}" sh "$deb_sh" dnsmasq-config-entry 2>/dev/null
    }
    [ "$(entry)" = 'server=/example.org/10.0.0.1' ] || fail "debian entry: should print only keen-pbr's output"
    [ "$(FAKE_BIN=fail entry)" = "partial
$fb" ] || fail "debian entry: a failing binary should be followed by the fallback servers"
    [ "$(BIN=$droot/missing entry)" = "$fb" ] || fail "debian entry: a missing binary should print the fallback servers"
}

test_resolver_generation() {
    resolver_bin=${KEEN_PBR_RESOLVER_BIN:-}
    [ -n "$resolver_bin" ] || return 0
    [ -x "$resolver_bin" ] || fail "resolver: binary is not executable"

    resolver_root="$tmp_root/resolver"
    empty_path="$resolver_root/empty-path"
    mkdir -p "$empty_path" "$resolver_root/cache"
    enabled_config="$resolver_root/enabled.json"
    cat > "$enabled_config" <<EOF
{
  "daemon": {"firewall_backend": "auto", "cache_dir": "$resolver_root/cache"},
  "outbounds": [{"type": "interface", "tag": "wan", "interface": "wan"}],
  "lists": {"domains": {"domains": ["example.com"]}},
  "dns": {
    "resolver_integration": "dnsmasq",
    "servers": [{"tag": "up", "address": "1.1.1.1"}],
    "rules": [{"list": ["domains"], "server": "up"}]
  },
  "route": {"rules": [{"default_gateway": "ipv4", "outbound": "wan"}]}
}
EOF
    output=$(PATH="$empty_path" "$resolver_bin" --config "$enabled_config" \
        generate-resolver-config dnsmasq) || fail "resolver: enabled generation failed"
    printf '%s\n' "$output" | grep -qx 'server=/example.com/1.1.1.1' ||
        fail "resolver: DNS rule was not generated"
    printf '%s\n' "$output" | grep -Eq '^txt-record=config-hash\.keen\.pbr,[0-9a-f]{32}\|[0-9]+\|[0-9]+$' ||
        fail "resolver: config hash TXT stamp is missing"
    ! printf '%s\n' "$output" | grep -q 'config-hash.keen.pbr,00000000000000000000000000000000' ||
        fail "resolver: managed output carries the off stamp"
    case "$output" in
        *"generate-resolver-config failed"*) fail "resolver: enabled generation reported failure" ;;
    esac

    disabled_config="$resolver_root/disabled.json"
    sed 's/"dnsmasq"/"none"/' "$enabled_config" > "$disabled_config"
    output=$(PATH="$empty_path" "$resolver_bin" --config "$disabled_config" \
        generate-resolver-config dnsmasq) || fail "resolver: disabled generation failed"
    printf '%s\n' "$output" | grep -qx '# keen-pbr: dnsmasq management is switched off' ||
        fail "resolver: disabled integration output is wrong"
    ! printf '%s\n' "$output" | grep -q 'example.com' || fail "resolver: disabled output has DNS rules"
    printf '%s\n' "$output" | grep -Eq '^txt-record=config-hash\.keen\.pbr,0{32}\|' ||
        fail "resolver: disabled output lacks the off stamp"

    invalid_config="$resolver_root/invalid.json"
    sed 's/"server": "up"/"server": "missing"/' "$enabled_config" > "$invalid_config"
    rc=0
    output=$(PATH="$empty_path" "$resolver_bin" --config "$invalid_config" \
        generate-resolver-config dnsmasq) || rc=$?
    [ "$rc" = 0 ] || fail "resolver: invalid config changed conf-script exit status"
    case "$output" in
        '# keen-pbr: generate-resolver-config failed:'*) ;;
        *) fail "resolver: invalid config did not produce a failure comment" ;;
    esac
    printf '%s\n' "$output" | grep -Eq '^txt-record=config-hash\.keen\.pbr,0{32}\|' ||
        fail "resolver: invalid config output lacks the off stamp"
}

echo "Testing OpenWrt dnsmasq hook..."
(test_openwrt)
(test_openwrt_upgrade_from_331)
(test_openwrt_boot)
(test_openwrt_uninstall)
echo "Testing Keenetic dnsmasq hook..."
(test_keenetic)
(test_keenetic_uninstall)
(test_keenetic_recommended)
(test_keenetic_entry_script)
echo "Testing Debian dnsmasq hook..."
(test_debian)
(test_debian_missing_dnsmasq)
(test_debian_uninstall)
(test_debian_recommended)
(test_debian_entry_script)
echo "Testing resolver generation..."
(test_resolver_generation)

echo "dnsmasq hooks tests: PASS"
