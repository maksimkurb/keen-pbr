#!/usr/bin/env bash
set -euo pipefail

backend=${1:-all}
source_root=${KPBR_SOURCE_ROOT:?KPBR_SOURCE_ROOT is required}
source_bin=${KPBR_SOURCE_BIN:?KPBR_SOURCE_BIN is required}
host_uid=${KPBR_HOST_UID:?KPBR_HOST_UID is required}
host_gid=${KPBR_HOST_GID:?KPBR_HOST_GID is required}
host_netns=${KPBR_HOST_NETNS:?KPBR_HOST_NETNS is required}
host_userns=${KPBR_HOST_USERNS:?KPBR_HOST_USERNS is required}

runtime=/run/keen-pbr-it
sandbox_repo=/mnt/repo
payload=/mnt/payload

log() { printf 'KPBR_IT_EVENT backend=harness case=suite stage=%s status=%s\n' "$1" "${2:-ok}"; }
die() { printf 'KPBR_IT_END backend=harness status=error message=%s\n' "${1// /_}" >&2; exit 2; }

run_stage() {
  local stage=$1 command_name=$2 status
  shift 2
  if "$@"; then
    return 0
  else
    status=$?
  fi
  # Keep diagnostics to stable script/phase names: command arguments can
  # contain test data, while the phase is sufficient to locate the failure.
  printf 'KPBR_IT_DIAG backend=harness case=suite stage=%s command=%s exit=%s\n' \
    "$stage" "$command_name" "$status" >&2
  return "$status"
}

namespace_guard() {
  local current_netns current_userns map_inner map_outer map_count gid_inner gid_outer gid_count
  [[ "$host_uid" != 0 ]] || die "host uid 0 is forbidden"
  [[ ${EUID:-$(id -u)} -eq 0 ]] || die "sandbox must be uid 0 only inside the user namespace"

  read -r map_inner map_outer map_count _ < /proc/self/uid_map
  [[ "$map_inner" == 0 && "$map_outer" == "$host_uid" && "$map_count" == 1 ]] ||
    die "unexpected uid_map; refusing to run with broader host credentials"
  read -r gid_inner gid_outer gid_count _ < /proc/self/gid_map
  [[ "$gid_inner" == 0 && "$gid_outer" == "$host_gid" && "$gid_count" == 1 ]] ||
    die "unexpected gid_map; refusing to run with broader host credentials"

  current_netns=$(readlink /proc/self/ns/net)
  current_userns=$(readlink /proc/self/ns/user)
  [[ "$current_netns" != "$host_netns" ]] || die "network namespace isolation failed"
  [[ "$current_userns" != "$host_userns" ]] || die "user namespace isolation failed"

  # With --pid --fork --mount-proc the sandbox init must be pid 1 in its PID ns.
  [[ $$ -eq 1 ]] || die "PID namespace isolation failed (sandbox is not PID 1)"
}


freeze_inherited_mounts() {
  local target options
  # Any inherited mount that is not one of our explicitly-private writable
  # mounts must become read-only.  This also catches separate /data, /opt,
  # cgroup, resolv.conf, etc. mounts that a simple read-only remount of / would
  # otherwise leave writable.
  while IFS= read -r target; do
    case "$target" in
      /proc|/proc/*|/dev|/dev/*|/run|/run/*|/tmp|/tmp/*|/var/cache|/var/cache/*|/var/tmp|/var/tmp/*|/home|/home/*|/mnt|/mnt/*|/var/lib/docker|/var/lib/docker/*)
        continue
        ;;
    esac
    options=$(findmnt -no OPTIONS --target "$target" 2>/dev/null || true)
    [[ -n "$options" ]] || continue
    if [[ ",$options," != *,ro,* ]]; then
      mount -o remount,bind,ro "$target" "$target" 2>/dev/null ||
        die "cannot remount inherited mount read-only: $target"
    fi
  done < <(findmnt -Rrn -o TARGET / | awk '{print length, $0}' | sort -rn | cut -d' ' -f2-)
}

mount_tmpfs() {
  local target=$1 mode=$2 extra=${3:-}
  [[ -d "$target" ]] || return 0
  if [[ -n "$extra" ]]; then
    mount -t tmpfs -o "mode=$mode,nosuid,nodev,$extra" tmpfs "$target"
  else
    mount -t tmpfs -o "mode=$mode,nosuid,nodev" tmpfs "$target"
  fi
}

prepare_mount_sandbox() {
  local binary_rel root_options repo_options
  case "$source_bin" in
    "$source_root"/*) binary_rel=${source_bin#"$source_root"/} ;;
    *) die "binary escaped repository path" ;;
  esac

  mount --make-rprivate /

  # Create private filesystems for imported test inputs.  Rootless user
  # namespaces cannot bind-mount this host's Btrfs mounts, so import only the
  # files the suite needs instead of exposing the host repository mount.
  mount -t tmpfs -o mode=0755,nosuid,nodev tmpfs /mnt
  mkdir -p "$sandbox_repo"
  mount -t tmpfs -o mode=0755,nosuid,nodev tmpfs "$sandbox_repo"
  mkdir -p "$sandbox_repo/tests/integration" "$sandbox_repo/frontend/dist"
  cp -a --no-preserve=ownership "$source_root/tests/integration/." \
    "$sandbox_repo/tests/integration/"
  if [[ -d "$source_root/frontend/dist" ]]; then
    cp -a --no-preserve=ownership "$source_root/frontend/dist/." \
      "$sandbox_repo/frontend/dist/"
  fi
  mkdir -p "$sandbox_repo/$(dirname "$binary_rel")"
  cp -a --no-preserve=ownership "$source_bin" "$sandbox_repo/$binary_rel"
  mount -o remount,ro tmpfs "$sandbox_repo"

  mkdir -p "$payload/tests/integration"
  ln -s "$sandbox_repo/tests/integration/container" "$payload/tests/integration/container"

  # Freeze the inherited host root filesystem.  All writes required by the
  # harness are redirected to private tmpfs mounts below.
  mount -o remount,bind,ro / /

  mount_tmpfs /run 0755
  mount_tmpfs /tmp 1777
  mount_tmpfs /var/cache 0755
  mount_tmpfs /var/tmp 1777
  mount_tmpfs /home 0755
  mount_tmpfs /var/lib/docker 0755
  if [[ -d /var/lib/docker ]]; then
    mount -o remount,ro tmpfs /var/lib/docker
  fi
  mount_tmpfs /dev/shm 1777 noexec

  freeze_inherited_mounts

  mkdir -p "$runtime" /run/netns /tmp/home
  chmod 0700 "$runtime" /tmp/home

  # Re-check the two filesystems whose mutability matters most.  No write probe
  # is performed because an unexpectedly writable host path must never be
  # modified even as a test.
  root_options=$(findmnt -no OPTIONS /)
  repo_options=$(findmnt -no OPTIONS -T "$sandbox_repo")
  [[ ",$root_options," == *,ro,* ]] || die "host root filesystem is not read-only in sandbox"
  [[ ",$repo_options," == *,ro,* ]] || die "repository filesystem is not read-only in sandbox"

  export KPBR_REPO_ROOT="$sandbox_repo"
  export KPBR_BIN="$sandbox_repo/$binary_rel"
  export KPBR_RUNTIME="$runtime"
  export KPBR_CONFIG_PATH="$runtime/config.json"
  export KPBR_SSH_KEY="$runtime/not-used-by-netns-ssh-shim"
  export KPBR_IT_SUMMARY="$runtime/summary.json"
  export KPBR_IT_DIAGNOSTICS="$runtime/case-diagnostics.log"
  export HOME=/tmp/home
  export TMPDIR=/tmp
  export PATH="$sandbox_repo/tests/integration/netns/shims:$PATH"

  [[ -x "$KPBR_BIN" ]] || die "sandbox binary is not executable"
  hostname kpbr-integration >/dev/null 2>&1 || true
}

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  if [[ -n ${KPBR_REPO_ROOT:-} ]]; then
    bash "$KPBR_REPO_ROOT/tests/integration/netns/service-control.sh" stop-all >/dev/null 2>&1 || true
    bash "$KPBR_REPO_ROOT/tests/integration/netns/fixtures.sh" stop >/dev/null 2>&1 || true
    bash "$KPBR_REPO_ROOT/tests/integration/netns/topology.sh" down >/dev/null 2>&1 || true
  fi
  if [[ $status -ne 0 && -f ${KPBR_IT_DIAGNOSTICS:-/nonexistent} ]]; then
    printf '%s\n' '--- preserved integration diagnostics ---' >&2
    tail -n 500 "$KPBR_IT_DIAGNOSTICS" >&2 || true
  fi
  exit "$status"
}

run_one_backend() {
  local current=$1 status
  export KPBR_IT_SUMMARY="$runtime/summary-$current.json"
  export KPBR_IT_DIAGNOSTICS="$runtime/case-diagnostics-$current.log"
  : >"$KPBR_IT_DIAGNOSTICS"
  printf 'KPBR_IT_BEGIN backend=%s case=suite stage=netns\n' "$current"
  set +e
  python3 "$payload/tests/integration/container/test-system.py" "$current"
  status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    printf 'KPBR_IT_DIAG backend=%s case=suite stage=artifacts message=private_runtime_%s\n' \
      "$current" "$runtime" >&2
  elif [[ ${INTEGRATION_VERBOSE:-0} == 1 ]]; then
    tail -n 200 "$runtime/keen-pbr.log" 2>/dev/null || true
    tail -n 100 "$runtime/dnsmasq.log" 2>/dev/null || true
  fi
  return "$status"
}

main() {
  local aggregate=0 current
  namespace_guard
  prepare_mount_sandbox
  trap cleanup EXIT INT TERM

  log isolation pass
  run_stage rootless_netfilter_preflight preflight.sh \
    bash "$KPBR_REPO_ROOT/tests/integration/netns/preflight.sh" "$backend"
  run_stage topology topology.sh \
    bash "$KPBR_REPO_ROOT/tests/integration/netns/topology.sh" up
  run_stage fixtures fixtures.sh \
    bash "$KPBR_REPO_ROOT/tests/integration/netns/fixtures.sh" start

  case "$backend" in
    all) for current in iptables nftables; do run_one_backend "$current" || aggregate=1; done ;;
    iptables|nftables) run_one_backend "$backend" || aggregate=1 ;;
    *) die "invalid backend" ;;
  esac

  printf 'KPBR_IT_END backend=all case=suite status=%s\n' "$([[ $aggregate == 0 ]] && echo pass || echo fail)"
  return "$aggregate"
}

if [[ "${KPBR_SANDBOX_SOURCE_ONLY:-0}" != 1 ]]; then
  main "$@"
fi
