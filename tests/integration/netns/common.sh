#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/requirements.sh"

: "${KPBR_REPO_ROOT:?rootless netns harness environment is not initialized}"
: "${KPBR_RUNTIME:?rootless netns harness runtime is not initialized}"

NS_CLIENT=kpbr-client
NS_DIRECT=kpbr-direct
NS_PBR=kpbr-pbr
fixture_dir="$KPBR_REPO_ROOT/tests/integration/container"

log() { printf 'KPBR_IT_EVENT backend=harness case=suite stage=%s status=%s\n' "$1" "${2:-ok}"; }
die() { printf 'KPBR_IT_END backend=harness status=error message=%s\n' "${1// /_}" >&2; exit 2; }

require_commands() {
  local command
  for command in "$@"; do
    command -v "$command" >/dev/null 2>&1 || die "required command not found: $command"
  done
}

ns_exists() {
  ip netns list | awk '{print $1}' | grep -Fxq "$1"
}

ns_exec() {
  local namespace=$1
  shift
  ip netns exec "$namespace" "$@"
}

pid_alive() {
  local pidfile=$1 pid
  [[ -r "$pidfile" ]] || return 1
  read -r pid <"$pidfile" || return 1
  [[ "$pid" =~ ^[0-9]+$ ]] || return 1
  kill -0 "$pid" 2>/dev/null
}

stop_pidfile() {
  local pidfile=$1 pid
  [[ -r "$pidfile" ]] || return 0
  read -r pid <"$pidfile" || true
  if [[ "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid" 2>/dev/null || true
    local _
    for _ in $(seq 1 50); do
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.05
    done
    kill -KILL "$pid" 2>/dev/null || true
  fi
  rm -f "$pidfile"
}
