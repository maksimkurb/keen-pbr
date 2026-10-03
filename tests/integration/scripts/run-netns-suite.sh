#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
sandbox_script="$repo_root/tests/integration/netns/sandbox.sh"
requirements_script="$repo_root/tests/integration/netns/requirements.sh"
backend=${1:-all}
binary=${2:-${INTEGRATION_BIN:-}}
integration_cases=${INTEGRATION_CASES:-all}
integration_verbose=${INTEGRATION_VERBOSE:-0}

host_uid=$(id -u)
host_gid=$(id -g)
host_netns=$(readlink /proc/self/ns/net)
host_userns=$(readlink /proc/self/ns/user)

die() {
  printf 'KPBR_IT_END backend=harness status=error message=%s\n' "${1// /_}" >&2
  exit 2
}

select_backends() {
  case "$1" in
    all|iptables|nftables) ;;
    *) die "INTEGRATION_BACKEND must be all, iptables, or nftables" ;;
  esac
  [[ "$integration_cases" =~ ^(all|[a-z0-9_]+(,[a-z0-9_]+)*)$ ]] ||
    die "INTEGRATION_CASES must be all or comma-separated case names"
  [[ "$integration_verbose" == 0 || "$integration_verbose" == 1 ]] ||
    die "INTEGRATION_VERBOSE must be 0 or 1"
}

source "$requirements_script"

require_launcher_prerequisites() {
  check_harness_requirements "$repo_root/tests/integration/netns/shims"
}

verify_unprivileged_userns() {
  local message
  if ! message=$(setpriv --no-new-privs unshare --user --map-root-user --mount --net --pid --fork --mount-proc /bin/true 2>&1); then
    printf '%s\n' "$message" >&2
    die "required unprivileged user/mount/pid/network namespaces are unavailable; no privileged fallback is allowed"
  fi
}

resolve_binary() {
  [[ -n "$binary" ]] || die "INTEGRATION_BIN is required (normally built by make integration-netns-build)"
  binary=$(realpath "$binary")
  [[ -x "$binary" ]] || die "keen-pbr integration binary is not executable: $binary"
  case "$binary" in
    "$repo_root"/*) ;;
    *) die "INTEGRATION_BIN must live inside the repository so the sandbox can bind it read-only" ;;
  esac
}

main() {
  select_backends "$backend"
  require_launcher_prerequisites

  # Running the launcher through sudo defeats the safety invariant.  The only
  # uid 0 used by this harness must be uid 0 *inside* a child user namespace,
  # mapped to the invoking unprivileged uid on the host.
  [[ ${EUID:-$host_uid} -ne 0 ]] ||
    die "do not run the netns harness as root or through sudo"
  [[ "$host_uid" != 0 ]] || die "real host root is explicitly unsupported"

  resolve_binary
  verify_unprivileged_userns

  printf 'KPBR_IT_BEGIN backend=%s case=suite stage=rootless_sandbox\n' "$backend"

  # no_new_privs is set before namespace creation and inherited by every test
  # process.  There is deliberately no sudo/doas/root fallback anywhere here.
  exec setpriv --no-new-privs \
    unshare \
      --user --map-root-user \
      --mount --propagation private \
      --net \
      --pid --fork --kill-child=SIGKILL --mount-proc \
      --uts --ipc \
      env \
        KPBR_HOST_UID="$host_uid" \
        KPBR_HOST_GID="$host_gid" \
        KPBR_HOST_NETNS="$host_netns" \
        KPBR_HOST_USERNS="$host_userns" \
        KPBR_SOURCE_ROOT="$repo_root" \
        KPBR_SOURCE_BIN="$binary" \
        INTEGRATION_CASES="$integration_cases" \
        INTEGRATION_VERBOSE="$integration_verbose" \
        bash "$sandbox_script" "$backend"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
