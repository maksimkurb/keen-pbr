#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_root=$(mktemp -d "${TMPDIR:-/tmp}/keen-pbr-dnsmasq.XXXXXX")
trap 'rm -rf "$tmp_root"' EXIT HUP INT TERM

fail() {
    echo "dnsmasq helper test: $*" >&2
    exit 1
}

assert_file_contains() {
    grep -Fqx "$2" "$1" || fail "$1 does not contain: $2"
}

assert_file_not_contains() {
    if [ -f "$1" ] && grep -Fqx "$2" "$1"; then
        fail "$1 unexpectedly contains: $2"
    fi
}

fake_bin="$tmp_root/bin"
mkdir -p "$fake_bin"
printf '%s\n' '#!/bin/sh' '[ "${DNSMASQ_TEST_FAIL_RESTART:-0}" = 1 ] && exit 7' 'exit 0' > "$fake_bin/systemctl"
printf '%s\n' '#!/bin/sh' 'exit 0' > "$fake_bin/logger"
printf '%s\n' '#!/bin/sh' "printf '%s\\n' 'server=/example.test/192.0.2.53'" > "$fake_bin/keen-pbr"
chmod +x "$fake_bin/systemctl" "$fake_bin/logger" "$fake_bin/keen-pbr"

run_debian() {
    DNSMASQ_CONF="$tmp_root/debian.conf" \
    DNSMASQ_DROPIN="$tmp_root/debian.d/keen-pbr.conf" \
    DNSMASQ_FALLBACK_FILE="$tmp_root/fallback.conf" \
    STATE_DIR="$tmp_root/debian-state" \
    KEEN_PBR_BIN="$fake_bin/keen-pbr" \
    PATH="$fake_bin:$PATH" \
    "$repo_root/packages/debian/files/usr/lib/keen-pbr/dnsmasq.sh" "$@"
}

debian_conf="$tmp_root/debian.conf"
printf '%s\n' 'server=198.51.100.53' > "$debian_conf"
run_debian activate
assert_file_contains "$tmp_root/debian.d/keen-pbr.conf" '# BEGIN keen-pbr managed block'
run_debian installed
run_debian deactivate
assert_file_not_contains "$tmp_root/debian.d/keen-pbr.conf" '# BEGIN keen-pbr managed block'

mkdir -p "$tmp_root/debian.d"
printf '%s\n' 'conf-file=/foreign.conf' > "$tmp_root/debian.d/keen-pbr.conf"
if run_debian activate; then fail 'foreign Debian drop-in was accepted'; fi
assert_file_contains "$tmp_root/debian.d/keen-pbr.conf" 'conf-file=/foreign.conf'
rm -f "$tmp_root/debian.d/keen-pbr.conf"

printf '%s\n' '# BEGIN keen-pbr managed block' \
    'conf-script=/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry' \
    '# END keen-pbr managed block' 'foreign-after=keep' > "$tmp_root/debian.d/keen-pbr.conf"
before=$(cksum "$tmp_root/debian.d/keen-pbr.conf")
if run_debian activate; then fail 'mixed Debian drop-in was accepted'; fi
[ "$(cksum "$tmp_root/debian.d/keen-pbr.conf")" = "$before" ] || fail 'mixed Debian drop-in was overwritten'
rm -f "$tmp_root/debian.d/keen-pbr.conf"

printf '%s\n%s\n%s\n' '# BEGIN keen-pbr managed block' \
    'conf-script=/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry' 'foreign=keep' > "$debian_conf"
before=$(cksum "$debian_conf")
if run_debian deactivate; then fail 'malformed Debian block was accepted'; fi
[ "$(cksum "$debian_conf")" = "$before" ] || fail 'malformed Debian block was truncated'

cat > "$debian_conf" <<'EOF'
foreign-before=keep
# BEGIN keen-pbr managed block
conf-script=/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry
# END keen-pbr managed block
foreign-after=keep
EOF
run_debian deactivate
assert_file_contains "$debian_conf" 'foreign-before=keep'
assert_file_contains "$debian_conf" 'foreign-after=keep'

cat > "$debian_conf" <<'EOF'
foreign-before=keep
# BEGIN keen-pbr managed block
conf-script=/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry
# BEGIN nested foreign block
foreign-inside=keep
# END keen-pbr managed block
foreign-after=keep
EOF
before=$(cksum "$debian_conf")
if run_debian deactivate; then fail 'nested malformed Debian block was accepted'; fi
[ "$(cksum "$debian_conf")" = "$before" ] || fail 'nested malformed Debian block was truncated'

rm -f "$debian_conf"
run_debian activate
if DNSMASQ_TEST_FAIL_RESTART=1 run_debian deactivate; then
    fail 'Debian deactivate hid a dnsmasq restart failure'
fi
assert_file_contains "$tmp_root/debian.d/keen-pbr.conf" '# BEGIN keen-pbr managed block'

run_keenetic() {
    DNSMASQ_CONF="$tmp_root/keenetic.conf" \
    DNSMASQ_INIT="$tmp_root/S56dnsmasq" \
    KEEN_PBR_BIN="$fake_bin/keen-pbr" \
    PATH="$fake_bin:$PATH" \
    "$repo_root/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/dnsmasq.sh" "$@"
}

printf '%s\n' 'server=198.51.100.54' > "$tmp_root/keenetic.conf"
printf '%s\n' '#!/bin/sh' '[ "${DNSMASQ_TEST_FAIL_RESTART:-0}" = 1 ] && exit 7' 'exit 0' > "$tmp_root/S56dnsmasq"
chmod +x "$tmp_root/S56dnsmasq"
run_keenetic activate
run_keenetic installed
run_keenetic deactivate
assert_file_contains "$tmp_root/keenetic.conf" 'server=198.51.100.54'
assert_file_not_contains "$tmp_root/keenetic.conf" 'conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry'

echo 'dnsmasq helper ownership/rollback tests: PASS'
