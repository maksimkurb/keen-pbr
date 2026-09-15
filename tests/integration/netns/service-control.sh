#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

keen_child_pid="$KPBR_RUNTIME/keen-pbr.child.pid"
keen_daemon_pid="$KPBR_RUNTIME/keen-pbr.daemon.pid"
dns_child_pid="$KPBR_RUNTIME/dnsmasq.child.pid"
dns_pid="$KPBR_RUNTIME/dnsmasq.pid"
keen_log="$KPBR_RUNTIME/keen-pbr.log"
dns_log="$KPBR_RUNTIME/dnsmasq.log"
resolver_conf="$KPBR_RUNTIME/resolver.conf"
fallback_conf="$KPBR_RUNTIME/dnsmasq-fallback.conf"

ensure_dns_files() {
  mkdir -p "$KPBR_RUNTIME"
  if [[ ! -f "$fallback_conf" ]]; then
    cat >"$fallback_conf" <<EOF_FALLBACK
# Private integration fallback: never use an external resolver.
server=10.20.0.2#15353
EOF_FALLBACK
  fi
  if [[ ! -f "$resolver_conf" ]]; then
    printf 'conf-file=%s\n' "$fallback_conf" >"$resolver_conf"
  fi
}

append_resolver_options() {
  local config=$1 line
  while IFS= read -r line || [[ -n "$line" ]]; do
    case "$line" in
      ''|'#'*) continue ;;
      conf-file=*) append_resolver_options "${line#conf-file=}" ;;
      *) resolver_options+=("--$line") ;;
    esac
  done <"$config"
}

start_dnsmasq() {
  require_commands dnsmasq setsid
  stop_pidfile "$dns_child_pid"
  rm -f "$dns_pid"
  ensure_dns_files
  resolver_options=()
  append_resolver_options "$resolver_conf"
  : >>"$dns_log"
  # --no-daemon keeps dnsmasq in debug mode, which also avoids its privileged
  # user/group drop.  A rootless user namespace cannot call setgroups(2)
  # unless it is given broader group mappings; retaining namespace-root here
  # keeps the harness single-mapped and fail-closed.
  setsid dnsmasq --no-daemon \
    --port=53 --listen-address=192.0.2.1 --bind-interfaces \
    --no-resolv --no-hosts --conf-file= --log-facility=- \
    "${resolver_options[@]}" </dev/null >>"$dns_log" 2>&1 &
  printf '%s\n' "$!" >"$dns_child_pid"

  local _
  for _ in $(seq 1 50); do
    if ! pid_alive "$dns_child_pid"; then
      tail -n 80 "$dns_log" >&2 || true
      die "dnsmasq exited during startup"
    fi
    if dig +time=1 +tries=1 +short fixture-ready.test @192.0.2.1 >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  tail -n 80 "$dns_log" >&2 || true
  die "dnsmasq did not become ready"
}

stop_dnsmasq() {
  stop_pidfile "$dns_child_pid"
  rm -f "$dns_pid"
}

start_keen() {
  require_commands setsid curl
  stop_pidfile "$keen_child_pid"
  rm -f "$keen_daemon_pid"
  [[ -r "$KPBR_CONFIG_PATH" ]] || die "keen-pbr config is missing: $KPBR_CONFIG_PATH"
  : >>"$keen_log"

  setsid "$KPBR_BIN" \
    --config "$KPBR_CONFIG_PATH" \
    --pid-file "$keen_daemon_pid" \
    --log-target stderr \
    service </dev/null >>"$keen_log" 2>&1 &
  printf '%s\n' "$!" >"$keen_child_pid"

  local ready=0 body _
  for _ in $(seq 1 160); do
    if ! pid_alive "$keen_child_pid"; then
      tail -n 160 "$keen_log" >&2 || true
      die "keen-pbr exited during startup"
    fi
    body=$(curl --silent --max-time 1 http://127.0.0.1:12121/api/health/service 2>/dev/null || true)
    if printf '%s' "$body" | grep -Eq '"status"[[:space:]]*:[[:space:]]*"running"'; then
      ready=1
      break
    fi
    sleep 0.05
  done
  [[ $ready == 1 ]] || {
    tail -n 160 "$keen_log" >&2 || true
    die "keen-pbr API did not reach running state"
  }

  # Make the DNS view deterministic before the test starts.  The daemon also
  # calls this hook on lifecycle/config changes; this initial call is idempotent.
  "$KPBR_REPO_ROOT/tests/integration/netns/resolver-hook.sh" reload
}

stop_keen() {
  stop_pidfile "$keen_child_pid"
  rm -f "$keen_daemon_pid" /run/keen-pbr/control.sock "$KPBR_RUNTIME/control.sock"
  # Match the Debian unit's deactivate semantics for the next dnsmasq start,
  # without restarting anything during teardown.
  ensure_dns_files
  printf 'conf-file=%s\n' "$fallback_conf" >"$resolver_conf"
}

status_unit() {
  local unit=$1 pidfile
  case "$unit" in
    keen-pbr|keen-pbr.service) pidfile=$keen_child_pid ;;
    dnsmasq|dnsmasq.service) pidfile=$dns_child_pid ;;
    *) return 3 ;;
  esac
  if pid_alive "$pidfile"; then
    printf '%s active pid=%s\n' "$unit" "$(cat "$pidfile")"
    return 0
  fi
  printf '%s inactive\n' "$unit"
  return 3
}

mainpid() {
  local unit=$1 pidfile
  case "$unit" in
    keen-pbr|keen-pbr.service) pidfile=$keen_child_pid ;;
    dnsmasq|dnsmasq.service) pidfile=$dns_child_pid ;;
    *) return 3 ;;
  esac
  if pid_alive "$pidfile"; then cat "$pidfile"; else printf '0\n'; fi
}

signal_keen() {
  local signal=$1
  pid_alive "$keen_child_pid" || return 3
  kill -s "$signal" "$(cat "$keen_child_pid")"
}

case "${1:-}" in
  start-keen) start_keen ;;
  stop-keen) stop_keen ;;
  restart-keen) stop_keen; start_keen ;;
  start-dnsmasq) start_dnsmasq ;;
  stop-dnsmasq) stop_dnsmasq ;;
  restart-dnsmasq) stop_dnsmasq; start_dnsmasq ;;
  stop-all) stop_keen; stop_dnsmasq ;;
  status) status_unit "${2:?unit required}" ;;
  mainpid) mainpid "${2:?unit required}" ;;
  signal-keen) signal_keen "${2:?signal required}" ;;
  *)
    echo "usage: service-control.sh start-keen|stop-keen|restart-keen|start-dnsmasq|stop-dnsmasq|restart-dnsmasq|stop-all|status UNIT|mainpid UNIT|signal-keen SIGNAL" >&2
    exit 2
    ;;
esac
