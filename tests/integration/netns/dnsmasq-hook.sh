#!/usr/bin/env bash
# Test KEEN_PBR_DNSMASQ_HOOK for the rootless netns harness:
#   dnsmasq-hook.sh inspect|apply <selector>|remove <selector>|status|alive
#
# Mirrors the platform hooks: apply installs a drop-in with
# `conf-script=<keen-pbr> --config <cfg> generate-resolver-config dnsmasq`
# and restarts the harness dnsmasq (service-control.sh translates the drop-in
# into options, since dnsmasq refuses foreign-uid config files in the sandbox).
# Every invocation except `alive` and `inspect` is appended to $KPBR_RUNTIME/dnsmasq-hook.log.
# `alive` exits 0 while the harness dnsmasq runs and 1 when it does not.
# If $KPBR_RUNTIME/dnsmasq-hook.fail exists, apply fails.
# If $KPBR_RUNTIME/dnsmasq-hook.nodropin exists, apply "succeeds" and restarts
# dnsmasq but installs no drop-in (like a hook writing to a directory dnsmasq
# does not read), so keen-pbr must notice the missing config-hash TXT stamp.
set -euo pipefail

: "${KPBR_REPO_ROOT:=/mnt/repo}"
: "${KPBR_RUNTIME:=/run/keen-pbr-it}"
: "${KPBR_BIN:=/mnt/repo/cmake-build-netns-integration/keen-pbr}"
: "${KPBR_CONFIG_PATH:=$KPBR_RUNTIME/config.json}"

service="$KPBR_REPO_ROOT/tests/integration/netns/service-control.sh"
conf_dir="$KPBR_RUNTIME/dnsmasq.d"
conf_file="$conf_dir/keen-pbr-upstream-dns.conf"
call_log="$KPBR_RUNTIME/dnsmasq-hook.log"
fail_flag="$KPBR_RUNTIME/dnsmasq-hook.fail"
nodropin_flag="$KPBR_RUNTIME/dnsmasq-hook.nodropin"

mkdir -p "$KPBR_RUNTIME"
# `alive` and `inspect` are queries the daemon may repeat; they are not logged.
[[ "${1:-}" == alive || "${1:-}" == inspect ]] || printf '%s\n' "${1:-}" >>"$call_log"

if [[ "${1:-}" == apply || "${1:-}" == remove ]] && [[ "${2:-}" != test:netns ]]; then
  echo "unknown dnsmasq selector: ${2:-}" >&2
  exit 3
fi

case "${1:-}" in
  inspect)
    want="conf-script=$KPBR_BIN --config $KPBR_CONFIG_PATH generate-resolver-config dnsmasq"
    pid="$(bash "$service" mainpid dnsmasq 2>/dev/null || echo 0)"
    state=absent
    if [[ -f "$conf_file" ]]; then
      if [[ "$(cat "$conf_file")" == "$want" ]]; then state=current; else state=foreign; fi
    fi
    printf 'hook_version=2\nplatform=test\nbinary=%s\n' "$(command -v dnsmasq || true)"
    printf '[instance]\nselector=test:netns\nenabled=1\nmain_pid=%s\n' "${pid:-0}"
    printf 'dropin_include=managed\ndropin_dir=%s\ndropin_file=%s\n' "$conf_dir" "$conf_file"
    printf 'dropin_content=%s\ndropin_state=%s\n' "$want" "$state"
    printf 'static_endpoint=192.0.2.1:53\npreflight=skip\n'
    ;;
  apply)
    if [[ -e "$fail_flag" ]]; then
      echo "simulated hook failure" >&2
      exit 1
    fi
    if [[ -e "$nodropin_flag" ]]; then
      rm -f "$conf_file"
      bash "$service" restart-dnsmasq
      exit 0
    fi
    mkdir -p "$conf_dir"
    printf 'conf-script=%s --config %s generate-resolver-config dnsmasq\n' \
      "$KPBR_BIN" "$KPBR_CONFIG_PATH" >"$conf_file"
    bash "$service" restart-dnsmasq
    ;;
  remove)
    [[ -e "$conf_file" ]] || exit 0
    rm -f "$conf_file"
    bash "$service" restart-dnsmasq
    ;;
  status)
    if [[ -f "$conf_file" ]]; then echo installed; else echo not-installed; fi
    ;;
  alive)
    # service-control.sh `status` exits 0 for a running unit, 3 otherwise.
    if bash "$service" status dnsmasq >/dev/null 2>&1; then exit 0; else exit 1; fi
    ;;
  *)
    echo "usage: $0 apply|remove|status|alive" >&2
    exit 64
    ;;
esac
