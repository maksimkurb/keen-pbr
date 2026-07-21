#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cache_root="$repo_root/build/integration-cache/qemu"
image_url=${QEMU_DEBIAN_IMAGE_URL:-https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2}
base_image="$cache_root/debian-12-genericcloud-amd64.qcow2"
integration_cases=${INTEGRATION_CASES:-all}
integration_verbose=${INTEGRATION_VERBOSE:-0}
integration_timeout=${INTEGRATION_TIMEOUT:-12m}

die() { echo "KPBR_IT_END backend=harness status=error message=${1// /_}" >&2; exit 2; }

select_backends() {
  case "$1" in
    all) backends=(iptables nftables) ;;
    iptables|nftables) backends=("$1") ;;
    *) die "INTEGRATION_BACKEND must be all, iptables, or nftables" ;;
  esac
  [[ "$integration_cases" =~ ^(all|[a-z0-9_]+(,[a-z0-9_]+)*)$ ]] ||
    die "INTEGRATION_CASES must be all or comma-separated case names"
  [[ "$integration_verbose" == 0 || "$integration_verbose" == 1 ]] ||
    die "INTEGRATION_VERBOSE must be 0 or 1"
}

require_prerequisites() {
  local command
  for command in qemu-system-x86_64 qemu-img mkfs.vfat mcopy mtype wget timeout realpath ssh-keygen python3; do
    command -v "$command" >/dev/null || die "QEMU integration tests require $command"
  done
}

package_input_hash() {
  {
    printf 'release=%s\n' "$(bash "$repo_root/build_scripts/resolve-version.sh" release "$repo_root")"
    find "$repo_root/CMakeLists.txt" "$repo_root/version.mk" \
      "$repo_root/include" "$repo_root/src" "$repo_root/packages/common" \
      "$repo_root/packages/debian/full" "$repo_root/build_scripts" \
      "$repo_root/docker/Dockerfile.debian-builder" "$repo_root/frontend" \
      -path '*/node_modules' -prune -o -path '*/dist' -prune -o \
      -path '*/__pycache__' -prune -o -name '*.pyc' -prune -o -type f -print0 |
      sort -z | xargs -0 sha256sum
  } | sha256sum | awk '{print $1}'
}

resolve_package() {
  local requested_deb=$1 force_rebuild=$2 package_hash cache_deb built_deb
  if [[ -n "$requested_deb" ]]; then
    deb=$requested_deb
  else
    package_hash="$(package_input_hash)"
    cache_deb="$repo_root/build/integration-cache/debian/$package_hash/keen-pbr.deb"
    if [[ "$force_rebuild" != 1 && -f "$cache_deb" ]]; then
      deb=$cache_deb
      echo "KPBR_IT_EVENT backend=harness case=suite stage=package status=cached"
    else
      command -v docker >/dev/null ||
        die "Docker is required to build the package; set INTEGRATION_DEB to a prebuilt package"
      docker info >/dev/null 2>&1 ||
        die "Docker is inaccessible; start Docker or set INTEGRATION_DEB to a prebuilt package"
      echo "KPBR_IT_EVENT backend=harness case=suite stage=package status=building"
      make -C "$repo_root" deb-packages DEBIAN_VARIANTS=full
      built_deb="$(find "$repo_root/build/packages/debian" -type f -name 'keen-pbr_*.deb' \
        ! -name '*dbgsym*' -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)"
      [[ -f "$built_deb" ]] || die "full Debian package build did not produce a .deb"
      mkdir -p "$(dirname "$cache_deb")"
      install -m 0644 "$built_deb" "$cache_deb"
      deb=$cache_deb
    fi
  fi
  [[ -f "$deb" ]] || die "full keen-pbr Debian package not found: $deb"
  deb="$(realpath "$deb")"
}

prepare_base_image() {
  mkdir -p "$cache_root"
  if [[ ! -f "$base_image" ]]; then
    echo "KPBR_IT_EVENT backend=harness case=suite stage=image status=downloading"
    wget -q -O "$base_image.tmp" "$image_url"
    mv "$base_image.tmp" "$base_image"
  fi
}

prepare_payload() {
  local workdir=$1 payload="$workdir/payload.img" payload_dir="$workdir/payload"
  mkdir -p "$payload_dir/tests/integration"
  install -m 0644 "$deb" "$payload_dir/keen-pbr.deb"
  install -m 0755 "$repo_root/tests/integration/vm/guest-run.sh" "$payload_dir/guest-run.sh"
  install -m 0755 "$repo_root/tests/integration/vm/router-run.sh" "$payload_dir/router-run.sh"
  install -m 0755 "$repo_root/tests/integration/vm/client-run.sh" "$payload_dir/client-run.sh"
  install -m 0755 "$repo_root/tests/integration/vm/wan-run.sh" "$payload_dir/wan-run.sh"
  install -m 0644 "$repo_root/tests/integration/vm/guest-lib.sh" "$payload_dir/guest-lib.sh"
  install -m 0600 "$workdir/router-key" "$payload_dir/router-key"
  cp -a "$repo_root/tests/integration/container" "$payload_dir/tests/integration/"
  mkfs.vfat -C -n KPBRDATA "$payload" 786432 >/dev/null
  mcopy -s -i "$payload" "$payload_dir"/* ::
}

write_network_config() {
  local role=$1 path=$2
  case "$role" in
    router)
      cat >"$path" <<'EOF'
version: 2
ethernets:
  mgmt0: {match: {macaddress: "52:54:00:00:00:11"}, set-name: mgmt0, dhcp4: true, optional: true}
  lan0:
    match: {macaddress: "52:54:00:00:00:12"}
    set-name: lan0
    addresses: [192.0.2.1/24, "2001:db8:1::1/64"]
  wan_direct:
    match: {macaddress: "52:54:00:00:00:13"}
    set-name: wan_direct
    addresses: [10.10.0.1/24, "2001:db8:10::1/64"]
  wan_pbr:
    match: {macaddress: "52:54:00:00:00:14"}
    set-name: wan_pbr
    addresses: [10.20.0.1/24, "2001:db8:20::1/64"]
EOF
      ;;
    client)
      cat >"$path" <<'EOF'
version: 2
ethernets:
  mgmt0: {match: {macaddress: "52:54:00:00:00:21"}, set-name: mgmt0, dhcp4: true, optional: true}
  lan0:
    match: {macaddress: "52:54:00:00:00:22"}
    set-name: lan0
    addresses: [192.0.2.2/24, "2001:db8:1::2/64"]
    routes:
      - {to: 0.0.0.0/0, via: 192.0.2.1, metric: 500}
      - {to: "::/0", via: "2001:db8:1::1", metric: 500}
EOF
      ;;
    wan)
      cat >"$path" <<'EOF'
version: 2
ethernets:
  mgmt0: {match: {macaddress: "52:54:00:00:00:31"}, set-name: mgmt0, dhcp4: true, optional: true}
  wan_direct: {match: {macaddress: "52:54:00:00:00:32"}, set-name: wan_direct, optional: true}
  wan_pbr: {match: {macaddress: "52:54:00:00:00:33"}, set-name: wan_pbr, optional: true}
EOF
      ;;
  esac
}

prepare_role_disk() {
  local current_backend=$1 workdir=$2 role=$3 public_key=$4
  local role_dir="$workdir/$role" seed="$workdir/$role/seed.img"
  mkdir -p "$role_dir"
  printf 'instance-id: keen-pbr-%s-%s-%s\nlocal-hostname: kpbr-%s\n' \
    "$current_backend" "$role" "$(date +%s%N)" "$role" >"$role_dir/meta-data"
  cat >"$role_dir/user-data" <<EOF
#cloud-config
disable_root: false
ssh_pwauth: false
users:
  - name: root
    lock_passwd: true
    ssh_authorized_keys:
      - $public_key
runcmd:
  - [ bash, -c, 'mkdir -p /mnt/payload && mount -o ro /dev/vdc /mnt/payload && exec bash /mnt/payload/guest-run.sh $role $current_backend $integration_cases' ]
EOF
  write_network_config "$role" "$role_dir/network-config"
  mkfs.vfat -C -n CIDATA "$seed" 16384 >/dev/null
  mcopy -i "$seed" "$role_dir/meta-data" ::meta-data
  mcopy -i "$seed" "$role_dir/user-data" ::user-data
  mcopy -i "$seed" "$role_dir/network-config" ::network-config
  qemu-img create -f qcow2 -F qcow2 -b "$base_image" "$role_dir/overlay.qcow2" >/dev/null
}

prepare_vm_disks() {
  local current_backend=$1 workdir=$2 public_key role
  ssh-keygen -q -t ed25519 -N '' -f "$workdir/router-key"
  public_key=$(<"$workdir/router-key.pub")
  prepare_payload "$workdir"
  for role in client wan router; do
    prepare_role_disk "$current_backend" "$workdir" "$role" "$public_key"
  done
}

allocate_ports() {
  python3 - <<'PY'
import socket
sockets = [socket.socket() for _ in range(3)]
for item in sockets:
    item.bind(("127.0.0.1", 0))
print(*(item.getsockname()[1] for item in sockets))
for item in sockets:
    item.close()
PY
}

qemu_common() {
  local workdir=$1 role=$2 memory=$3 mac=$4
  exec timeout --kill-after=5 "$integration_timeout" qemu-system-x86_64 \
    -machine accel=kvm:tcg -m "$memory" -smp 2 -display none \
    -serial "file:$workdir/$role/serial.log" -no-reboot \
    -drive "file=$workdir/$role/overlay.qcow2,if=virtio,format=qcow2" \
    -drive "file=$workdir/$role/seed.img,if=virtio,format=raw" \
    -drive "file=$workdir/payload.img,if=virtio,format=raw,readonly=on" \
    -netdev user,id=mgmt -device "virtio-net-pci,netdev=mgmt,mac=$mac" "${@:5}"
}

kill_vm_group() {
  local pid
  for pid in "${router_pid:-}" "${client_pid:-}" "${wan_pid:-}"; do
    [[ -n "$pid" ]] && kill "$pid" >/dev/null 2>&1 || true
  done
}

execute_vm() {
  local workdir=$1 lan_port direct_port pbr_port group_failed=0 peer_wait
  read -r lan_port direct_port pbr_port < <(allocate_ports)
  set +e
  qemu_common "$workdir" client 768 52:54:00:00:00:21 \
    -netdev "socket,id=lan,listen=127.0.0.1:$lan_port" \
    -device virtio-net-pci,netdev=lan,mac=52:54:00:00:00:22 \
    >"$workdir/client/qemu.log" 2>&1 &
  client_pid=$!
  qemu_common "$workdir" wan 768 52:54:00:00:00:31 \
    -netdev "socket,id=direct,listen=127.0.0.1:$direct_port" \
    -device virtio-net-pci,netdev=direct,mac=52:54:00:00:00:32 \
    -netdev "socket,id=pbr,listen=127.0.0.1:$pbr_port" \
    -device virtio-net-pci,netdev=pbr,mac=52:54:00:00:00:33 \
    >"$workdir/wan/qemu.log" 2>&1 &
  wan_pid=$!
  sleep 1
  if ! kill -0 "$client_pid" 2>/dev/null || ! kill -0 "$wan_pid" 2>/dev/null; then
    group_failed=1
  else
    qemu_common "$workdir" router 1536 52:54:00:00:00:11 \
      -netdev "socket,id=lan,connect=127.0.0.1:$lan_port" \
      -device virtio-net-pci,netdev=lan,mac=52:54:00:00:00:12 \
      -netdev "socket,id=direct,connect=127.0.0.1:$direct_port" \
      -device virtio-net-pci,netdev=direct,mac=52:54:00:00:00:13 \
      -netdev "socket,id=pbr,connect=127.0.0.1:$pbr_port" \
      -device virtio-net-pci,netdev=pbr,mac=52:54:00:00:00:14 \
      >"$workdir/router/qemu.log" 2>&1 &
    router_pid=$!
    while kill -0 "$router_pid" 2>/dev/null; do
      if ! kill -0 "$client_pid" 2>/dev/null || ! kill -0 "$wan_pid" 2>/dev/null; then
        group_failed=1
        kill "$router_pid" >/dev/null 2>&1 || true
        break
      fi
      sleep 0.25
    done
  fi
  if [[ -n ${router_pid:-} ]]; then wait "$router_pid"; qemu_status=$?; else qemu_status=1; fi
  for peer_wait in $(seq 1 40); do
    if ! kill -0 "$client_pid" 2>/dev/null && ! kill -0 "$wan_pid" 2>/dev/null; then break; fi
    sleep 0.25
  done
  kill_vm_group
  wait "$client_pid"; client_qemu_status=$?
  wait "$wan_pid"; wan_qemu_status=$?
  [[ $group_failed == 0 ]] || qemu_status=1
  set -e
}

extract_results() {
  local workdir=$1
  local role
  guest_status="$(mtype -i "$workdir/router/seed.img" ::result 2>/dev/null | tr -d '[:space:]' || true)"
  for role in router client wan; do
    mcopy -i "$workdir/$role/seed.img" ::provision.log "$workdir/$role/provision.log" >/dev/null 2>&1 || true
  done
  mcopy -i "$workdir/router/seed.img" ::summary.json "$workdir/summary.json" >/dev/null 2>&1 || true
  mcopy -i "$workdir/router/seed.img" ::diagnostics.txt "$workdir/router/diagnostics.txt" >/dev/null 2>&1 || true
  mcopy -i "$workdir/router/seed.img" ::case-diagnostics.log "$workdir/router/case-diagnostics.log" >/dev/null 2>&1 || true
}

print_marked_diagnostics() {
  local backend=$1 workdir=$2
  local router_log="$workdir/router/serial.log" role
  if grep -q 'KPBR_IT_' "$router_log" 2>/dev/null; then
    tr -d '\r' <"$router_log" | grep -E 'KPBR_IT_(BEGIN|EVENT|DIAG|END)' | tail -n 240 >&2 || true
  else
    echo "KPBR_IT_DIAG backend=$backend case=suite stage=boot message=no_guest_marker_seen" >&2
    tail -n 80 "$router_log" 2>/dev/null | sed 's/^/KPBR_IT_DIAG backend='"$backend"' case=suite stage=boot role=router message=/' >&2 || true
  fi
  for role in client wan; do
    tail -n 60 "$workdir/$role/serial.log" 2>/dev/null | cut -c1-1000 |
      sed 's/^/KPBR_IT_DIAG backend='"$backend"' case=suite stage=boot role='"$role"' message=/' >&2 || true
  done
  if [[ -f "$workdir/router/diagnostics.txt" ]]; then
    head -n 80 "$workdir/router/diagnostics.txt" | cut -c1-1000 |
      sed 's/^/KPBR_IT_DIAG backend='"$backend"' case=suite stage=guest message=/' >&2
  fi
}

print_backend_summary() {
  local backend=$1 workdir=$2 status=$3
  if [[ -f "$workdir/summary.json" ]]; then
    if ! python3 - "$workdir/summary.json" <<'PY'
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
for result in data.get("results", []):
    print("KPBR_IT_END backend={backend} case={case} status={status} duration_ms={duration_ms}".format(**result))
PY
    then
      echo "KPBR_IT_END backend=$backend case=suite status=$status duration_ms=unknown"
    fi
  else
    echo "KPBR_IT_END backend=$backend case=suite status=$status duration_ms=unknown"
  fi
}

run_backend() {
  local current_backend=$1 workdir backend_failed=0
  workdir="$(mktemp -d "$cache_root/run-${current_backend}.XXXXXX")"
  echo "KPBR_IT_BEGIN backend=$current_backend case=suite stage=vm"
  prepare_vm_disks "$current_backend" "$workdir"
  execute_vm "$workdir"
  extract_results "$workdir"
  if [[ "$qemu_status" != 0 || "$guest_status" != 0 ]]; then
    backend_failed=1
    print_marked_diagnostics "$current_backend" "$workdir"
    echo "KPBR_IT_DIAG backend=$current_backend case=suite stage=artifacts message=retained_at_$workdir" >&2
  elif [[ "$integration_verbose" == 1 ]]; then
    tr -d '\r' <"$workdir/router/serial.log" | grep -E 'KPBR_IT_(BEGIN|EVENT|DIAG|END)' || true
  fi
  print_backend_summary "$current_backend" "$workdir" "$([[ $backend_failed == 0 ]] && echo pass || echo fail)"
  if [[ $backend_failed == 0 ]]; then
    rm -rf "$workdir"
  fi
  return "$backend_failed"
}

main() {
  local backend=${1:-all} requested_deb=${2:-} force_rebuild=${3:-0} aggregate=0 current
  select_backends "$backend"
  require_prerequisites
  resolve_package "$requested_deb" "$force_rebuild"
  prepare_base_image
  for current in "${backends[@]}"; do
    if ! run_backend "$current"; then aggregate=1; fi
  done
  echo "KPBR_IT_END backend=all case=suite status=$([[ $aggregate == 0 ]] && echo pass || echo fail)"
  return "$aggregate"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
