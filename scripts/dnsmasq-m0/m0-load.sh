#!/bin/sh
# Bounded load-time measurement: loopback only, temp dir, kill -9 cleanup.
# usage: m0-load.sh <mode: servers|conf> <count> <cap_seconds>
set -u
mode="$1"; n="$2"; cap="$3"
DNSMASQ=/usr/sbin/dnsmasq
W="$(mktemp -d /tmp/kpbr-m0l.XXXXXX)"
PIDS=""
cleanup() { for p in $PIDS; do kill -9 "$p" 2>/dev/null; done; rm -rf "$W"; }
trap cleanup EXIT INT TERM
now_ms() { awk '{printf "%d\n", $1 * 1000}' /proc/uptime; }
for u in 5399:198.51.100.1 5400:198.51.100.2; do
    port="${u%%:*}"; ip="${u#*:}"
    nice -n 19 "$DNSMASQ" --keep-in-foreground --no-resolv --no-hosts --listen-address=127.0.0.1 \
        --bind-interfaces --port="$port" --address=/m0canary.test/"$ip" --conf-file=/dev/null \
        --pid-file= --user=root &
    PIDS="$PIDS $!"
done
gen() {
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
gen 5399 > "$W/s.conf"
echo "file_bytes=$(wc -c < "$W/s.conf") lines=$(wc -l < "$W/s.conf")"
if [ "$mode" = servers ]; then src="--servers-file=$W/s.conf"; else src="--conf-file=$W/s.conf"; fi
t0=$(now_ms)
nice -n 19 "$DNSMASQ" --keep-in-foreground --no-resolv --no-hosts --listen-address=127.0.0.1 --bind-interfaces \
    --port=5398 "$src" --pid-file= --cache-size=0 --user=root &
main=$!; PIDS="$PIDS $main"
q() { dig @127.0.0.1 -p 5398 m0canary.test +short +time=1 +tries=1 2>/dev/null | head -1; }
wait_answer() {  # $1 = expected ip; prints elapsed ms or TIMEOUT
    s=$(now_ms)
    while :; do
        a="$(q)"
        [ "$a" = "$1" ] && { echo $(( $(now_ms) - s )); return 0; }
        kill -0 "$main" 2>/dev/null || { echo DIED; return 1; }
        [ $(( $(now_ms) - s )) -gt $(( cap * 1000 )) ] && { echo TIMEOUT; return 1; }
        usleep 200000 2>/dev/null || sleep 1
    done
}
cpu() { set -- $(sed 's/.*) //' /proc/$main/stat); echo $(( (${12} + ${13}) * 10 )); }
rss() { awk '/VmRSS/{r=$2} /VmHWM/{h=$2} END{printf "rss_kb=%s hwm_kb=%s", r, h}' /proc/$main/status; }
r=$(wait_answer 198.51.100.1)
echo "mode=$mode n=$n first_answer_ms=$r cpu_ms=$(cpu) $(rss) since_start_ms=$(( $(now_ms) - t0 ))"
if [ "$mode" = servers ] && [ "$r" != TIMEOUT ] && [ "$r" != DIED ]; then
    gen 5400 > "$W/s.new" && mv "$W/s.new" "$W/s.conf"
    c0=$(cpu)
    kill -HUP "$main"
    r2=$(wait_answer 198.51.100.2)
    echo "mode=$mode n=$n sighup_flip_ms=$r2 sighup_cpu_ms=$(( $(cpu) - c0 )) $(rss)"
fi
