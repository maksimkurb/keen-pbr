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
  for command in qemu-system-x86_64 qemu-img mkfs.vfat mcopy mtype wget timeout realpath; do
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

prepare_vm_disks() {
  local current_backend=$1 workdir=$2
  local seed="$workdir/seed.img" payload="$workdir/payload.img"
  local payload_dir="$workdir/payload" metadata="$workdir/meta-data" userdata="$workdir/user-data"
  printf 'instance-id: keen-pbr-%s-%s\nlocal-hostname: keen-pbr-it\n' \
    "$current_backend" "$(date +%s)" >"$metadata"
  printf '%s\n' \
    '#cloud-config' 'users:' '  - name: root' '    lock_passwd: true' \
    'ssh_pwauth: false' 'runcmd:' \
    "  - [ bash, -c, 'mkdir -p /mnt/payload && mount -o ro /dev/vdc /mnt/payload && exec bash /mnt/payload/guest-run.sh $current_backend $integration_cases' ]" \
    >"$userdata"
  mkfs.vfat -C -n CIDATA "$seed" 16384 >/dev/null
  mcopy -i "$seed" "$metadata" ::meta-data
  mcopy -i "$seed" "$userdata" ::user-data
  mkdir -p "$payload_dir/tests/integration"
  install -m 0644 "$deb" "$payload_dir/keen-pbr.deb"
  install -m 0755 "$repo_root/tests/integration/vm/guest-run.sh" "$payload_dir/guest-run.sh"
  cp -a "$repo_root/tests/integration/container" "$payload_dir/tests/integration/"
  mkfs.vfat -C -n KPBRDATA "$payload" 786432 >/dev/null
  mcopy -s -i "$payload" "$payload_dir"/* ::
  qemu-img create -f qcow2 -F qcow2 -b "$base_image" "$workdir/overlay.qcow2" >/dev/null
}

execute_vm() {
  local workdir=$1
  set +e
  timeout "$integration_timeout" qemu-system-x86_64 \
    -machine accel=kvm:tcg -m 1536 -smp 2 -display none \
    -serial "file:$workdir/serial.log" -no-reboot \
    -drive "file=$workdir/overlay.qcow2,if=virtio,format=qcow2" \
    -drive "file=$workdir/seed.img,if=virtio,format=raw" \
    -drive "file=$workdir/payload.img,if=virtio,format=raw,readonly=on" \
    -netdev user,id=net0 -device virtio-net-pci,netdev=net0
  qemu_status=$?
  set -e
}

extract_results() {
  local workdir=$1
  guest_status="$(mtype -i "$workdir/seed.img" ::result 2>/dev/null | tr -d '[:space:]' || true)"
  mcopy -i "$workdir/seed.img" ::summary.json "$workdir/summary.json" >/dev/null 2>&1 || true
  mcopy -i "$workdir/seed.img" ::diagnostics.txt "$workdir/diagnostics.txt" >/dev/null 2>&1 || true
  mcopy -i "$workdir/seed.img" ::case-diagnostics.log "$workdir/case-diagnostics.log" >/dev/null 2>&1 || true
  mcopy -i "$workdir/seed.img" ::provision.log "$workdir/provision.log" >/dev/null 2>&1 || true
}

print_marked_diagnostics() {
  local backend=$1 workdir=$2
  if grep -q 'KPBR_IT_' "$workdir/serial.log" 2>/dev/null; then
    tr -d '\r' <"$workdir/serial.log" | grep -E 'KPBR_IT_(BEGIN|EVENT|DIAG|END)' | tail -n 240 >&2 || true
  else
    echo "KPBR_IT_DIAG backend=$backend case=suite stage=boot message=no_guest_marker_seen" >&2
    tail -n 80 "$workdir/serial.log" 2>/dev/null | sed 's/^/KPBR_IT_DIAG backend='"$backend"' case=suite stage=boot message=/' >&2 || true
  fi
  if [[ -f "$workdir/diagnostics.txt" ]]; then
    head -n 80 "$workdir/diagnostics.txt" | cut -c1-1000 |
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
    tr -d '\r' <"$workdir/serial.log" | grep -E 'KPBR_IT_(BEGIN|EVENT|DIAG|END)' || true
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
