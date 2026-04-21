#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
fixtures_dir="/opt/keen-pbr/firewall-it/fixtures"

declare -A images=(
  [iptables]="keen-pbr-firewall-it:iptables"
  [nftables]="keen-pbr-firewall-it:nftables"
)

declare -A dockerfiles=(
  [iptables]="tests/firewall_it/docker/Dockerfile.iptables"
  [nftables]="tests/firewall_it/docker/Dockerfile.nftables"
)

for backend in iptables nftables; do
  docker build -t "${images[$backend]}" -f "$repo_root/${dockerfiles[$backend]}" "$repo_root"
done

run_case() {
  local container="$1"
  local backend="$2"
  local config_name="$3"
  local setup_name="$4"
  shift 4

  docker exec "$container" /opt/keen-pbr/firewall-it/scripts/run-in-netns.sh \
    --backend "$backend" \
    --config "$fixtures_dir/$config_name" \
    --setup "$fixtures_dir/$setup_name" \
    "$@"
}

run_case_expect_failure() {
  local container="$1"
  local backend="$2"
  local config_name="$3"
  local setup_name="$4"
  local expected_substring="$5"
  shift 5

  local output=""
  local status=0
  if output="$(run_case "$container" "$backend" "$config_name" "$setup_name" "$@" 2>&1)"; then
    echo "expected failure for $config_name on backend=$backend, but command succeeded" >&2
    exit 1
  else
    status=$?
  fi

  if [[ $status -eq 0 ]]; then
    echo "expected non-zero exit for $config_name on backend=$backend" >&2
    exit 1
  fi

  if [[ "$output" != *"$expected_substring"* ]]; then
    echo "unexpected failure output for $config_name on backend=$backend" >&2
    echo "expected substring: $expected_substring" >&2
    echo "$output" >&2
    exit 1
  fi
}

for backend in iptables nftables; do
  container="keen-pbr-firewall-it-${backend}"
  docker rm -f "$container" >/dev/null 2>&1 || true
  docker run -d --name "$container" \
    --cap-add NET_ADMIN \
    --cap-add NET_RAW \
    --cap-add SYS_ADMIN \
    "${images[$backend]}" >/dev/null

  run_case "$container" "$backend" "firewall-smoke.json" "firewall-smoke.setup.sh"

  run_case "$container" "$backend" "urltest-reachable.json" "urltest-reachable.setup.sh" \
    --run-urltest-probes

  run_case "$container" "$backend" "firewall-rule-shapes.json" "firewall-rule-shapes.setup.sh"

  run_case "$container" "$backend" "firewall-table-interface.json" "firewall-table-interface.setup.sh"

  if [[ "$backend" == "iptables" ]]; then
    run_case_expect_failure \
      "$container" "$backend" \
      "firewall-iptables-multiport.json" \
      "firewall-iptables-multiport.setup.sh" \
      "iptables backend does not support combining src_port and dest_port"
  else
    run_case "$container" "$backend" \
      "firewall-iptables-multiport.json" \
      "firewall-iptables-multiport.setup.sh"
  fi

  docker rm -f "$container" >/dev/null
done
