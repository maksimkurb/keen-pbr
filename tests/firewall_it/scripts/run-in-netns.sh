#!/usr/bin/env bash
set -euo pipefail

backend=""
config=""
mode="destructive"
setup_script=""
run_urltest="0"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --backend)
      backend="$2"
      shift 2
      ;;
    --config)
      config="$2"
      shift 2
      ;;
    --mode)
      mode="$2"
      shift 2
      ;;
    --setup)
      setup_script="$2"
      shift 2
      ;;
    --run-urltest-probes)
      run_urltest="1"
      shift
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 1
      ;;
  esac
done

if [[ -z "$backend" || -z "$config" ]]; then
  echo "usage: run-in-netns.sh --backend <name> --config <path> [--mode <mode>] [--setup <path>] [--run-urltest-probes]" >&2
  exit 1
fi

suffix="$(date +%s)-$$"
client_ns="kpbr-client-${suffix}"
server_ns="kpbr-server-${suffix}"
server_pid=""

cleanup() {
  if [[ -n "$server_pid" ]]; then
    kill "$server_pid" >/dev/null 2>&1 || true
  fi
  ip netns del "$client_ns" >/dev/null 2>&1 || true
  ip netns del "$server_ns" >/dev/null 2>&1 || true
}
trap cleanup EXIT

ip netns add "$client_ns"
ip netns exec "$client_ns" ip link set lo up

export KPBR_CLIENT_NS="$client_ns"
export KPBR_SERVER_NS="$server_ns"

if [[ -n "$setup_script" ]]; then
  # shellcheck disable=SC1090
  source "$setup_script"
fi

cmd=(
  ip netns exec "$client_ns"
  /opt/keen-pbr/bin/keen-pbr-firewall-it
  --config "$config"
  --backend "$backend"
  --mode "$mode"
)

if [[ "$run_urltest" == "1" ]]; then
  cmd+=(--run-urltest-probes)
fi

"${cmd[@]}"
