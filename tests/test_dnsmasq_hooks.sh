#!/bin/sh
# Functional tests for the per-platform dnsmasq hooks (apply|remove|status|alive).

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_root=$(mktemp -d "${TMPDIR:-/tmp}/keen-pbr-dnsmasq-hooks.XXXXXX")
trap 'rm -rf "$tmp_root"' EXIT HUP INT TERM

fail() {
    echo "dnsmasq hooks test: $*" >&2
    exit 1
}

owrt_hook="$repo_root/packages/openwrt/keen-pbr/files/usr/lib/keen-pbr/dnsmasq-hook.sh"
owrt_uci_sh="$repo_root/packages/openwrt/keen-pbr/files/usr/lib/keen-pbr/uci.sh"
keen_dir="$repo_root/packages/keenetic/keen-pbr/files/opt"
keen_hook="$keen_dir/usr/lib/keen-pbr/dnsmasq-hook.sh"
keen_s55="$keen_dir/etc/init.d/S55keen-pbr-dnsmasq"
keen_migrate="$keen_dir/usr/lib/keen-pbr/migrate-dnsmasq.sh"
deb_hook="$repo_root/packages/debian/files/usr/lib/keen-pbr/dnsmasq-hook.sh"

for f in "$owrt_hook" "$keen_hook" "$keen_s55" "$deb_hook"; do
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

# Fake uci (same db format as test_dnsmasq_migration.sh).
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

test_openwrt() {
    confdir="$tmp_root/owrt/tmp/dnsmasq.d"
    export FAKE_UCI_DB="$tmp_root/uci.db" FAKE_UCI_LOG="$tmp_root/uci.log"
    export TMP_ROOT="$tmp_root/owrt/tmp"
    export CACHE_DIR="$tmp_root/owrt/cache" CONFIG_DIR="$tmp_root/owrt/etc-keen-pbr"
    export KEEN_PBR_BIN=/usr/sbin/keen-pbr
    mounts="/usr/sbin/keen-pbr $CONFIG_DIR $CACHE_DIR"
    cat > "$FAKE_UCI_DB" <<EOF
dhcp.cfg01=dnsmasq
dhcp.cfg01.confdir=$confdir
dhcp.cfg01.addnmount=/opt/foreign
dhcp.cfg02=dnsmasq
dhcp.cfg02.server=1.1.1.1
EOF
    : > "$tmp_root/uci.log"
    run() { PATH="$fake_bin:$PATH" UCI="$fake_bin/uci" sh "$owrt_hook" "$@"; }
    f1="$confdir/keen-pbr-upstream-dns.conf"
    f2="$TMP_ROOT/dnsmasq.cfg02.d/keen-pbr-upstream-dns.conf"
    want='conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq'

    [ "$(run status)" = not-installed ] || fail "owrt: status before apply"
    reset_restarts
    run apply || fail "owrt: apply failed"
    [ "$(cat "$f1")" = "$want" ] || fail "owrt: conf file 1 wrong"
    [ "$(cat "$f2")" = "$want" ] || fail "owrt: conf file 2 (default confdir) wrong"
    for m in $mounts; do
        grep -qxF "dhcp.cfg01.addnmount=$m" "$FAKE_UCI_DB" || fail "owrt: cfg01 missing mount $m"
        grep -qxF "dhcp.cfg02.addnmount=$m" "$FAKE_UCI_DB" || fail "owrt: cfg02 missing mount $m"
    done
    grep -qxF "dhcp.cfg01.addnmount=/opt/foreign" "$FAKE_UCI_DB" || fail "owrt: foreign mount lost"
    [ "$(restarts)" = 1 ] || fail "owrt: apply should restart once"
    [ "$(commits)" = 1 ] || fail "owrt: apply should commit once"
    [ "$(run status)" = installed ] || fail "owrt: status after apply"

    # Second apply: no rewrite, no commit, but restart again.
    touch -t 200001010000 "$f1" "$f2"
    cp "$FAKE_UCI_DB" "$tmp_root/uci.before"
    run apply || fail "owrt: second apply failed"
    [ "$(touched "$f1" "$f2")" = 0 ] || fail "owrt: second apply rewrote the conf file"
    cmp -s "$FAKE_UCI_DB" "$tmp_root/uci.before" || fail "owrt: second apply changed uci"
    [ "$(commits)" = 1 ] || fail "owrt: second apply committed"
    [ "$(restarts)" = 2 ] || fail "owrt: second apply should restart again"

    # Migration must not strip the hook's addnmount entries.
    PATH="$fake_bin:$PATH" DNSMASQ_RESTART_CMD="true" sh "$owrt_uci_sh" dnsmasq-migrate-from-keen-pbr >/dev/null 2>&1 ||
        fail "owrt: migration failed"
    cmp -s "$FAKE_UCI_DB" "$tmp_root/uci.before" || fail "owrt: migration stripped new addnmount entries"
    [ -f "$f1" ] || fail "owrt: migration removed the new conf file"
    [ "$(commits)" = 1 ] || fail "owrt: migration committed"

    # Remove.
    run remove || fail "owrt: remove failed"
    [ ! -e "$f1" ] && [ ! -e "$f2" ] || fail "owrt: conf files not removed"
    for m in $mounts; do
        ! grep -qF "addnmount=$m" "$FAKE_UCI_DB" || fail "owrt: mount $m not removed"
    done
    grep -qxF "dhcp.cfg01.addnmount=/opt/foreign" "$FAKE_UCI_DB" || fail "owrt: remove dropped foreign mount"
    [ "$(restarts)" = 3 ] || fail "owrt: remove should restart once"
    [ "$(commits)" = 2 ] || fail "owrt: remove should commit once"

    # Remove with nothing installed.
    cp "$FAKE_UCI_DB" "$tmp_root/uci.before"
    run remove || fail "owrt: idempotent remove failed"
    [ "$(restarts)" = 3 ] && [ "$(commits)" = 2 ] || fail "owrt: idempotent remove restarted/committed"
    cmp -s "$FAKE_UCI_DB" "$tmp_root/uci.before" || fail "owrt: idempotent remove changed uci"
    [ "$(run status)" = not-installed ] || fail "owrt: status after remove"

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

# --- Keenetic ------------------------------------------------------------------

test_keenetic() {
    kroot="$tmp_root/keen"
    mkdir -p "$kroot"
    export DNSMASQ_CONF="$kroot/dnsmasq.conf" INIT_SCRIPT="$kroot/S56dnsmasq"
    export TMP_CONF_DIR="$kroot/tmp/keen-pbr/dnsmasq.d"
    export DNSMASQ_SH=/opt/usr/lib/keen-pbr/dnsmasq.sh
    f="$TMP_CONF_DIR/keen-pbr-upstream-dns.conf"
    want='conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry'
    run() { sh "$keen_hook" "$@"; }

    # No dnsmasq installed.
    rm -f "$DNSMASQ_CONF" "$INIT_SCRIPT"
    reset_restarts
    rc=0
    err=$(run apply 2>&1 >/dev/null) || rc=$?
    [ "$rc" = 2 ] || fail "keenetic: apply without dnsmasq should exit 2 (got $rc)"
    [ "$err" = "dnsmasq is not installed (opkg install dnsmasq-full)" ] || fail "keenetic: wrong message: $err"
    [ "$(restarts)" = 0 ] || fail "keenetic: restarted without dnsmasq"

    printf 'user=nobody\nserver=1.1.1.1\n' > "$DNSMASQ_CONF"
    : > "$INIT_SCRIPT"
    [ "$(run status)" = not-installed ] || fail "keenetic: status before apply"

    # Nothing installed: remove is silent and writes nothing.
    cp "$DNSMASQ_CONF" "$kroot/conf.before"
    run remove || fail "keenetic: idempotent remove failed"
    cmp -s "$DNSMASQ_CONF" "$kroot/conf.before" || fail "keenetic: idempotent remove changed dnsmasq.conf"
    [ "$(restarts)" = 0 ] || fail "keenetic: idempotent remove restarted"

    run apply || fail "keenetic: apply failed"
    [ "$(cat "$f")" = "$want" ] || fail "keenetic: conf file wrong"
    grep -qxF '# BEGIN keen-pbr upstream dns' "$DNSMASQ_CONF" || fail "keenetic: block missing"
    grep -qxF "conf-dir=$TMP_CONF_DIR,*.conf" "$DNSMASQ_CONF" || fail "keenetic: conf-dir line missing"
    grep -qxF 'server=1.1.1.1' "$DNSMASQ_CONF" || fail "keenetic: user config lost"
    [ "$(restarts)" = 1 ] || fail "keenetic: apply should restart once"
    [ "$(run status)" = installed ] || fail "keenetic: status after apply"

    # Second apply: nothing rewritten.
    touch -t 200001010000 "$f" "$DNSMASQ_CONF"
    run apply || fail "keenetic: second apply failed"
    [ "$(touched "$f" "$DNSMASQ_CONF")" = 0 ] || fail "keenetic: second apply rewrote files"
    [ "$(grep -c 'BEGIN keen-pbr upstream dns' "$DNSMASQ_CONF")" = 1 ] || fail "keenetic: block duplicated"
    [ "$(restarts)" = 2 ] || fail "keenetic: second apply should restart again"

    # apply drops a fallback block written by the migration (exact match only).
    cp "$DNSMASQ_CONF" "$kroot/conf.before"
    printf 'server=8.8.8.8\nserver=8.8.4.4\n' > "$kroot/fallback.conf"
    export FALLBACK_CONF="$kroot/fallback.conf"
    fb_begin='# BEGIN keen-pbr fallback upstream (added on upgrade; replace or remove)'
    {
        cat "$kroot/conf.before"
        printf '%s\nserver=8.8.8.8\nserver=8.8.4.4\n# END keen-pbr fallback upstream\n' "$fb_begin"
    } > "$DNSMASQ_CONF"
    run apply || fail "keenetic: apply with fallback block failed"
    ! grep -q 'keen-pbr fallback upstream' "$DNSMASQ_CONF" || fail "keenetic: apply kept the fallback block"
    ! grep -q '8\.8\.8\.8' "$DNSMASQ_CONF" || fail "keenetic: apply left fallback servers"
    cmp -s "$DNSMASQ_CONF" "$kroot/conf.before" || fail "keenetic: apply changed other config"
    {
        cat "$kroot/conf.before"
        printf '%s\nserver=8.8.8.8\nserver=9.9.9.9\n# END keen-pbr fallback upstream\n' "$fb_begin"
    } > "$DNSMASQ_CONF"
    run apply || fail "keenetic: apply with modified fallback block failed"
    grep -qxF 'server=9.9.9.9' "$DNSMASQ_CONF" || fail "keenetic: apply removed a user-modified block"
    cp "$kroot/conf.before" "$DNSMASQ_CONF"
    unset FALLBACK_CONF
    reset_restarts

    # Legacy migration keeps the new block.
    cp "$DNSMASQ_CONF" "$kroot/conf.before"
    FALLBACK_CONF=/nonexistent sh "$keen_migrate" || fail "keenetic: migration failed"
    cmp -s "$DNSMASQ_CONF" "$kroot/conf.before" || fail "keenetic: migration modified the new block"
    # ...even next to a legacy block.
    {
        printf '# BEGIN keen-pbr managed block\n'
        printf 'conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry\n'
        printf '# END keen-pbr managed block\n'
        cat "$kroot/conf.before"
    } > "$DNSMASQ_CONF"
    FALLBACK_CONF=/nonexistent sh "$keen_migrate" || fail "keenetic: migration (legacy) failed"
    grep -qxF '# BEGIN keen-pbr upstream dns' "$DNSMASQ_CONF" || fail "keenetic: migration removed the new block"
    ! grep -q 'dnsmasq-config-entry' "$DNSMASQ_CONF" || fail "keenetic: legacy block survived"
    reset_restarts

    # S55 boot script: recreates the tmp file only when the block exists.
    rm -rf "$kroot/tmp"
    sh "$keen_s55" start
    [ "$(cat "$f")" = "$want" ] || fail "keenetic: S55 start did not recreate the conf file"
    rm -rf "$kroot/tmp"
    sh "$keen_s55" stop
    [ ! -e "$f" ] || fail "keenetic: S55 stop created the file"
    sh "$keen_s55" restart
    [ -f "$f" ] || fail "keenetic: S55 restart did not recreate the conf file"

    # Remove.
    run remove || fail "keenetic: remove failed"
    [ ! -e "$f" ] || fail "keenetic: conf file not removed"
    ! grep -q 'keen-pbr upstream dns' "$DNSMASQ_CONF" || fail "keenetic: block not removed"
    ! grep -q 'conf-dir=' "$DNSMASQ_CONF" || fail "keenetic: conf-dir line left"
    grep -qxF 'server=1.1.1.1' "$DNSMASQ_CONF" || fail "keenetic: user config lost on remove"
    [ "$(restarts)" = 1 ] || fail "keenetic: remove should restart once"
    rm -rf "$kroot/tmp"
    sh "$keen_s55" start
    [ ! -e "$f" ] || fail "keenetic: S55 start created the file without the block"

    # dnsmasq.sh dnsmasq-config-entry: fallback servers unless keen-pbr runs.
    keen_sh="$keen_dir/usr/lib/keen-pbr/dnsmasq.sh"
    sh -n "$keen_sh" || fail "keenetic: syntax error in dnsmasq.sh"
    printf 'server=8.8.8.8\nserver=8.8.4.4\n' > "$kroot/fallback.conf"
    printf '#!/bin/sh\ncase "${FAKE_BIN:-ok}" in\n  ok) echo "server=/example.org/10.0.0.1"; echo "# stamp" ;;\n  fail) echo "# keen-pbr: generate-resolver-config failed: Cannot open config file" ;;\n  empty) ;;\nesac\n' > "$kroot/keen-pbr"
    chmod +x "$kroot/keen-pbr"
    entry() {
        FALLBACK_CONF="$kroot/fallback.conf" KEEN_PBR_BIN="$kroot/keen-pbr" \
        PIDFILE="$kroot/keen-pbr.pid" WORK_DIR="$kroot/work" sh "$keen_sh" dnsmasq-config-entry 2>/dev/null
    }
    fb='server=8.8.8.8
server=8.8.4.4'
    rm -f "$kroot/keen-pbr.pid"
    [ "$(entry)" = "$fb" ] || fail "keenetic: entry without pid file should print the fallback"
    echo 999999 > "$kroot/keen-pbr.pid"
    [ "$(entry)" = "$fb" ] || fail "keenetic: entry with a stale pid should print the fallback"
    echo $$ > "$kroot/keen-pbr.pid"
    [ "$(entry)" = 'server=/example.org/10.0.0.1
# stamp' ] || fail "keenetic: entry while active should print only keen-pbr's config"
    [ "$(FAKE_BIN=fail entry)" = "$fb" ] || fail "keenetic: entry with a failing generator should print the fallback"
    [ "$(FAKE_BIN=empty entry)" = "$fb" ] || fail "keenetic: entry with an empty generator should print the fallback"
    [ ! -e "$kroot/work/dnsmasq-config-entry.$$" ] || fail "keenetic: entry left a temp file"

    # alive: Entware's rc.func `check` prints "alive."/"dead."; unknown without
    # the init script.
    alive_init="$kroot/S56alive"
    printf '#!/bin/sh\n[ "$1" = check ] || exit 64\ncase "${FAKE_STATE:-}" in\n  alive) echo " Checking dnsmasq... alive."; exit 0 ;;\n  dead) echo " Checking dnsmasq... dead."; exit 1 ;;\n  *) exit "${FAKE_RC:-5}" ;;\nesac\n' > "$alive_init"
    chmod +x "$alive_init"
    [ "$(INIT_SCRIPT="$alive_init" FAKE_STATE=alive rc_of run alive)" = 0 ] || fail "keenetic: alive"
    [ "$(INIT_SCRIPT="$alive_init" FAKE_STATE=dead rc_of run alive)" = 1 ] || fail "keenetic: dead"
    [ "$(INIT_SCRIPT="$alive_init" FAKE_STATE= FAKE_RC=5 rc_of run alive)" = 2 ] || fail "keenetic: unknown (odd output)"
    [ "$(INIT_SCRIPT="$kroot/missing" rc_of run alive)" = 2 ] || fail "keenetic: unknown (no init script)"
    [ "$(INIT_SCRIPT="$alive_init" ALIVE_CMD=false rc_of run alive)" = 1 ] || fail "keenetic: dead (ALIVE_CMD)"
    # A bare pidof must not be what decides (firmware dnsmasq).
    [ "$(PATH="$fake_bin:$PATH" FAKE_PIDOF=1 INIT_SCRIPT="$alive_init" FAKE_STATE=dead rc_of run alive)" = 1 ] ||
        fail "keenetic: pidof must not override the init script"
}

# --- Debian --------------------------------------------------------------------

test_debian() {
    droot="$tmp_root/deb"
    mkdir -p "$droot/etc/dnsmasq.d" "$droot/bin"
    export DNSMASQ_CONF_DIR="$droot/etc/dnsmasq.d" KEEN_PBR_BIN=/usr/sbin/keen-pbr
    f="$DNSMASQ_CONF_DIR/keen-pbr-upstream-dns.conf"
    want='conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq'
    run() { PATH="$droot/bin:$PATH" sh "$deb_hook" "$@"; }

    reset_restarts
    # Only meaningful when the host has no dnsmasq of its own.
    if ! command -v dnsmasq >/dev/null 2>&1; then
        rc=0
        run apply >/dev/null 2>&1 || rc=$?
        [ "$rc" = 2 ] || fail "debian: apply without dnsmasq should exit 2 (got $rc)"
        [ "$(restarts)" = 0 ] || fail "debian: restarted without dnsmasq"
    fi
    printf '#!/bin/sh\n' > "$droot/bin/dnsmasq"
    chmod +x "$droot/bin/dnsmasq"

    run remove || fail "debian: idempotent remove failed"
    [ "$(restarts)" = 0 ] || fail "debian: idempotent remove restarted"
    run apply || fail "debian: apply failed"
    [ "$(cat "$f")" = "$want" ] || fail "debian: conf file wrong"
    [ "$(restarts)" = 1 ] || fail "debian: apply should restart once"
    [ "$(run status)" = installed ] || fail "debian: status"
    touch -t 200001010000 "$f"
    run apply || fail "debian: second apply failed"
    [ "$(touched "$f")" = 0 ] || fail "debian: second apply rewrote file"
    [ "$(restarts)" = 2 ] || fail "debian: second apply should restart again"
    run remove || fail "debian: remove failed"
    [ ! -e "$f" ] || fail "debian: file not removed"
    [ "$(restarts)" = 3 ] || fail "debian: remove should restart once"
    [ "$(run status)" = not-installed ] || fail "debian: status after remove"

    # alive: 0 active, 1 inactive/failed, 2 unknown.
    [ "$(ALIVE_CMD=true rc_of run alive)" = 0 ] || fail "debian: alive (ALIVE_CMD ok)"
    [ "$(ALIVE_CMD=false rc_of run alive)" = 1 ] || fail "debian: dead (ALIVE_CMD 1)"
    [ "$(ALIVE_CMD="sh -c 'exit 7'" rc_of run alive)" = 2 ] || fail "debian: unknown (ALIVE_CMD 7)"
    # Default: systemctl is-active (fake systemctl), exit 3 means inactive.
    printf '#!/bin/sh\n[ "$1 $2 $3" = "is-active --quiet dnsmasq" ] && exit "${FAKE_ACTIVE:-3}"\nexit 9\n' > "$droot/bin/systemctl"
    chmod +x "$droot/bin/systemctl"
    [ "$(FAKE_ACTIVE=0 rc_of run alive)" = 0 ] || fail "debian: alive (systemctl active)"
    [ "$(FAKE_ACTIVE=3 rc_of run alive)" = 1 ] || fail "debian: dead (systemctl inactive)"
    rm -f "$droot/bin/systemctl"
    # Without systemctl the state is unknown (only checkable when the host has none).
    if ! command -v systemctl >/dev/null 2>&1; then
        [ "$(rc_of run alive)" = 2 ] || fail "debian: unknown (no systemctl)"
    fi
}

echo "Testing OpenWrt dnsmasq hook..."
test_openwrt
echo "Testing Keenetic dnsmasq hook..."
test_keenetic
echo "Testing Debian dnsmasq hook..."
test_debian

echo "dnsmasq hooks tests: PASS"
