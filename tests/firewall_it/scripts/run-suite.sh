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

for backend in iptables nftables; do
  container="keen-pbr-firewall-it-${backend}"
  docker rm -f "$container" >/dev/null 2>&1 || true
  docker run -d --name "$container" \
    --cap-add NET_ADMIN \
    --cap-add NET_RAW \
    --cap-add SYS_ADMIN \
    "${images[$backend]}" >/dev/null

  docker exec "$container" /opt/keen-pbr/firewall-it/scripts/run-in-netns.sh \
    --backend "$backend" \
    --config "$fixtures_dir/firewall-smoke.json" \
    --setup "$fixtures_dir/firewall-smoke.setup.sh"

  docker exec "$container" /opt/keen-pbr/firewall-it/scripts/run-in-netns.sh \
    --backend "$backend" \
    --config "$fixtures_dir/urltest-reachable.json" \
    --setup "$fixtures_dir/urltest-reachable.setup.sh" \
    --run-urltest-probes

  docker rm -f "$container" >/dev/null
done
