#!/bin/sh
# keen-pbr M0 experiments. Never edits production dnsmasq config.
set -u
DNSMASQ="${DNSMASQ:-$(command -v dnsmasq || echo /opt/sbin/dnsmasq)}"
WORK="$(mktemp -d /tmp/kpbr-m0.XXXXXX)" || exit 1
chmod 700 "$WORK"
# dnsmasq can ignore SIGTERM while busy loading a large servers-file, so escalate to SIGKILL after 1 s.
trap 'p=$(cat "$WORK"/*.pid 2>/dev/null); kill $p 2>/dev/null; sleep 1; kill -9 $p 2>/dev/null; rm -rf "$WORK"' EXIT INT TERM
say() { printf '%s\n' "$*"; }
now_ms() { awk '{printf "%d\n", $1 * 1000}' /proc/uptime; }
say "dnsmasq=$DNSMASQ version=$("$DNSMASQ" --version 2>/dev/null | head -1)"

exp_A() {
    cat > "$WORK/emit.sh" <<'EOF'
#!/bin/sh
: > "$(dirname "$0")/marker"
echo 'server=/m0.invalid/127.0.0.1#9'
echo 'txt-record=m0.invalid,ok'
echo 'rebind-domain-ok=/m0.invalid/'
echo 'address=/use-application-dns.net/'
echo 'no-resolv'
EOF
    cat > "$WORK/bad.sh" <<'EOF'
#!/bin/sh
echo 'this-option-does-not-exist=1'
EOF
    cat > "$WORK/fail.sh" <<'EOF'
#!/bin/sh
echo 'server=/m0.invalid/127.0.0.1#9'
exit 3
EOF
    chmod 700 "$WORK"/*.sh
    run() {  # $1 = case id, $2 = conf-script value
        rm -f "$WORK/marker"
        printf 'port=0\nconf-script=%s\n' "$2" > "$WORK/a.conf"
        out="$("$DNSMASQ" --test --conf-file="$WORK/a.conf" 2>&1)"; rc=$?
        m=no; [ -e "$WORK/marker" ] && m=yes
        say "$1 exit=$rc marker=$m output=$(printf '%s' "$out" | tr '\n' ' ' | cut -c1-200)"
    }
    run A1 "$WORK/emit.sh"           # expected: exit=0 marker=yes
    run A2 "$WORK/bad.sh"            # expected: exit!=0
    run A3 "$WORK/fail.sh"           # record: does a non-zero script exit fail --test?
    run A4 "/bin/sh $WORK/emit.sh"   # record: are arguments in conf-script supported?
    printf 'port=0\n' > "$WORK/base.conf"
    "$DNSMASQ" --test --conf-file="$WORK/base.conf" >/dev/null 2>&1; say "A5 baseline exit=$?"   # expected 0
}

exp_B() {
    exp_A
}

exp_C() {
    say "self netns: $(readlink /proc/self/ns/net)"
    for d in /proc/[0-9]*; do
        exe="$(readlink "$d/exe" 2>/dev/null)" || continue
        case "${exe##*/}" in dnsmasq*) ;; *) continue ;; esac
        p="${d#/proc/}"
        say "--- pid=$p exe=$exe ppid=$(awk '{print $4}' "$d/stat")"
        say "cmdline: $(tr '\0' ' ' < "$d/cmdline")"
        say "netns: $(readlink "$d/ns/net")"
        say "sockets: $(ls -l "$d/fd" 2>/dev/null | sed -n 's/.*socket:\[\([0-9]*\)\].*/\1/p' | tr '\n' ' ')"
    done
    say "--- /proc/net/udp";  cat /proc/net/udp
    say "--- /proc/net/udp6"; cat /proc/net/udp6 2>/dev/null
    say "--- /proc/net/tcp listeners"; awk '$4=="0A"' /proc/net/tcp
    for f in /var/etc/dnsmasq.conf.* /opt/etc/dnsmasq.conf /etc/dnsmasq.conf; do
        [ -f "$f" ] || continue
        say "--- $f"; grep -E '^(conf-dir|conf-file|port|listen-address|interface|except-interface|bind-interfaces|bind-dynamic)' "$f"
    done
    command -v uci >/dev/null && { say "--- uci"; uci -X show dhcp | grep '=dnsmasq'; }
    command -v ubus >/dev/null && { say "--- ubus"; ubus call service list '{"name":"dnsmasq"}'; }
    command -v systemctl >/dev/null && say "MainPID=$(systemctl show -p MainPID --value dnsmasq.service)"
}

exp_D() {   # $1 = main conf file, rest = extra args seen in the real cmdline (e.g. -7 /etc/dnsmasq.d,...)
    main="$1"; shift
    printf 'conf-script=%s generate-resolver-config dnsmasq\n' "${KEEN_PBR_BIN:-$(command -v keen-pbr)}" > "$WORK/cand.conf"
    t0=$(now_ms)
    out="$("$DNSMASQ" --test --conf-file="$main" "$@" --conf-file="$WORK/cand.conf" 2>&1)"; rc=$?
    say "D1 exit=$rc ms=$(( $(now_ms) - t0 )) output=$(printf '%s' "$out" | tail -c 300 | tr '\n' ' ')"
}
# OpenWrt:  m0-router.sh D /var/etc/dnsmasq.conf.cfg01411c
# Keenetic: m0-router.sh D /opt/etc/dnsmasq.conf
# Debian:   m0-router.sh D /etc/dnsmasq.conf -7 /etc/dnsmasq.d,.dpkg-dist,.dpkg-old,.dpkg-new

exp_E() {   # $1 = LAN IP to bind the main test instance, $2 = domain count (10000|100000|450000)
    lan="$1"; n="$2"
    tick=0.5; sleep 0.5 2>/dev/null || tick=1   # busybox sleep may lack fractions
    for u in 5399:198.51.100.1 5400:198.51.100.2; do
        port="${u%%:*}"; ip="${u#*:}"
        "$DNSMASQ" --keep-in-foreground --no-resolv --no-hosts --listen-address=127.0.0.1 \
            --bind-interfaces --port="$port" --address=/m0canary.test/"$ip" \
            --pid-file="$WORK/up$port.pid" --conf-file=/dev/null &
    done
    gen() {  # $1 = canary upstream port; 50 domains per line like DnsmasqGenerator
        awk -v n="$n" -v up="$1" 'BEGIN {
            printf "server=/m0canary.test/127.0.0.1#%s\n", up
            line = ""; c = 0
            for (i = 1; i <= n; i++) {
                line = line "/d" i ".m0bench.test"; c++
                if (c == 50) { printf "server=%s/127.0.0.1#9\n", line; line = ""; c = 0 }
            }
            if (c) printf "server=%s/127.0.0.1#9\n", line
        }'
    }
    gen 5399 > "$WORK/servers.conf"
    t0=$(now_ms)
    "$DNSMASQ" --keep-in-foreground --no-resolv --no-hosts --listen-address="$lan" --bind-interfaces \
        --port=5398 --servers-file="$WORK/servers.conf" --pid-file="$WORK/main.pid" \
        --conf-file=/dev/null --cache-size=1000 --user=root &
    # --user=root: dnsmasq re-reads servers-file on SIGHUP *after* dropping privileges;
    # $WORK is 0700 root. (Production M2 must make the file readable by the dnsmasq user, §20.)
    # wait up to 5 s for the three pid files (dnsmasq writes them at startup)
    deadline=$(( $(now_ms) + 5000 ))
    while [ "$(now_ms)" -lt "$deadline" ] && \
          { [ ! -s "$WORK/up5399.pid" ] || [ ! -s "$WORK/up5400.pid" ] || [ ! -s "$WORK/main.pid" ]; }; do
        sleep "$tick"
    done
    up1=$(cat "$WORK/up5399.pid" 2>/dev/null); up2=$(cat "$WORK/up5400.pid" 2>/dev/null)
    main=$(cat "$WORK/main.pid" 2>/dev/null)
    isalive() { [ -n "$1" ] && kill -0 "$1" 2>/dev/null && echo yes || echo no; }
    say "E check upstream5399 alive=$(isalive "$up1")"
    say "E check upstream5400 alive=$(isalive "$up2")"
    m_alive=$(isalive "$main")
    say "E check main alive=$m_alive"
    if [ "$m_alive" != yes ]; then
        printf 'E error: main dnsmasq (pid=%s) is not running; check the instance log above\n' "$main" >&2
        return 1
    fi
    # sockets: /proc/net/udp column 2 is local_address "IP:PORT" (hex, port in upper-case)
    for port in 5398 5399 5400; do
        hex=$(printf '%04X' "$port")
        if awk -v h="$hex" 'NR>1 { k = split($2, a, ":"); if (a[k] == h) f = 1 } END { exit !f }' /proc/net/udp; then
            b=yes
        else
            b=no
        fi
        say "E check udp $port bound=$b"
    done
    # wait for the servers-file to load: RSS stable for 3 polls, or 60 s max
    rss() { awk '/VmRSS/{print $2}' "/proc/$1/status" 2>/dev/null; }
    hwm() { awk '/VmHWM/{print $2}' "/proc/$1/status" 2>/dev/null; }
    prev=-1; flat=0; deadline=$(( $(now_ms) + 60000 ))
    while [ "$(now_ms)" -lt "$deadline" ]; do
        sleep "$tick"
        cur=$(rss "$main")
        [ -n "$cur" ] || break
        if [ "$cur" -gt "$prev" ]; then flat=0; else flat=$(( flat + 1 )); fi
        prev=$cur
        [ "$flat" -ge 3 ] && break
    done
    say "E start_ms~$(( $(now_ms) - t0 )) rss_kb=$(rss "$main") hwm_kb=$(hwm "$main")"
    say "If m0-query.py shows only timeouts, check the router firewall allows UDP $lan:5398 from the LAN (OpenWrt: lan zone input ACCEPT; Keenetic: may block), and test with: dig @$lan -p 5398 m0canary.test"
    say "Start m0-query.py on the PC now; press Enter to send SIGHUP"; read _
    gen 5400 > "$WORK/servers.conf.new" && mv "$WORK/servers.conf.new" "$WORK/servers.conf"
    say "E sighup_at_uptime_ms=$(now_ms)"; kill -HUP "$main"
    sleep 10
    say "E after rss_kb=$(awk '/VmRSS/{print $2}' /proc/$main/status) hwm_kb=$(awk '/VmHWM/{print $2}' /proc/$main/status) memavail_kb=$(awk '/MemAvailable/{print $2}' /proc/meminfo)"
    printf 'server=/broken\n' >> "$WORK/servers.conf"; kill -HUP "$main"; sleep 3
    say "E3 after invalid line: process alive=$(kill -0 "$main" 2>/dev/null && echo yes || echo no) (check m0-query.py: which answer now?)"
}

usage() {
    cat <<'EOF'
usage: m0-router.sh A|B|C|D|E [args]
  A                       experiment A (conf-script under --test)
  B                       experiment B (same as A; run with DNSMASQ=<old binary>)
  C                       experiment C (instance identification)
  D <main-conf> [args...] experiment D (preflight of real config)
  E <lan-ip> <count>      experiment E (servers-file + SIGHUP; M2 gate)
EOF
    exit 64
}

case "${1:-}" in
    A) exp_A;;
    B) exp_A;;
    C) exp_C;;
    D) shift
       [ $# -ge 1 ] || { printf '%s\n' "usage: m0-router.sh D <main-conf> [args...]" >&2; exit 64; }
       exp_D "$@";;
    E) shift
       [ $# -eq 2 ] || { printf '%s\n' "usage: m0-router.sh E <lan-ip> <count>" >&2; exit 64; }
       exp_E "$@";;
    *) usage;;
esac
