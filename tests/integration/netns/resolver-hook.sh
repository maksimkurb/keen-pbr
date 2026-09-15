#!/usr/bin/env bash
set -euo pipefail

: "${KPBR_REPO_ROOT:=/mnt/repo}"
: "${KPBR_RUNTIME:=/run/keen-pbr-it}"
: "${KPBR_BIN:=/mnt/repo/cmake-build-netns-integration/keen-pbr}"

service="$KPBR_REPO_ROOT/tests/integration/netns/service-control.sh"
resolver_conf="$KPBR_RUNTIME/resolver.conf"
fallback_conf="$KPBR_RUNTIME/dnsmasq-fallback.conf"
lock="$KPBR_RUNTIME/resolver-hook.lock"
action=${1:-reload}

mkdir -p "$KPBR_RUNTIME"
exec 9>"$lock"
flock 9

case "$action" in
  reload|activate|restart-dnsmasq)
    tmp="$resolver_conf.tmp.$$"
    if ! "$KPBR_BIN" generate-resolver-config dnsmasq >"$tmp"; then
      rm -f "$tmp"
      exit 1
    fi
    mv "$tmp" "$resolver_conf"
    bash "$service" restart-dnsmasq
    ;;
  deactivate)
    printf 'conf-file=%s\n' "$fallback_conf" >"$resolver_conf"
    bash "$service" restart-dnsmasq
    ;;
  *)
    echo "unknown resolver hook action: $action" >&2
    exit 2
    ;;
esac
