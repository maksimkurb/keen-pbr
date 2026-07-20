#!/usr/bin/env bash
set -euo pipefail

backend=${1:-all}
requested_deb=${2:-}
force_rebuild=${3:-0}
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cache_root="$repo_root/build/integration-cache/qemu"
image_url=${QEMU_DEBIAN_IMAGE_URL:-https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2}
base_image="$cache_root/debian-12-genericcloud-amd64.qcow2"

case "$backend" in
  all) backends=(iptables nftables) ;;
  iptables|nftables) backends=("$backend") ;;
  *) echo "INTEGRATION_BACKEND must be all, iptables, or nftables" >&2; exit 2 ;;
esac

package_input_hash() {
  {
    printf 'release=%s\n' "$(bash "$repo_root/build_scripts/resolve-version.sh" release "$repo_root")"
    find "$repo_root/CMakeLists.txt" "$repo_root/version.mk" \
      "$repo_root/include" "$repo_root/src" "$repo_root/packages/common" \
      "$repo_root/packages/debian/full" "$repo_root/build_scripts" \
      "$repo_root/docker/Dockerfile.debian-builder" "$repo_root/frontend" \
      -path '*/node_modules' -prune -o -path '*/dist' -prune -o \
      -path '*/__pycache__' -prune -o -name '*.pyc' -prune -o -type f -print0 | \
      sort -z | xargs -0 sha256sum
  } | sha256sum | awk '{print $1}'
}

if [[ -n "$requested_deb" ]]; then
  deb="$requested_deb"
else
  package_hash="$(package_input_hash)"
  cache_deb="$repo_root/build/integration-cache/debian/$package_hash/keen-pbr.deb"
  if [[ "$force_rebuild" != 1 && -f "$cache_deb" ]]; then
    deb="$cache_deb"
    echo "[integration-tests] Reusing cached Debian package: $deb"
  else
    make -C "$repo_root" deb-packages DEBIAN_VARIANTS=full
    built_deb="$(find "$repo_root/build/packages/debian" -type f -name 'keen-pbr_*.deb' ! -name '*dbgsym*' -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)"
    [[ -f "$built_deb" ]] || { echo "full keen-pbr Debian package build did not produce a .deb" >&2; exit 2; }
    mkdir -p "$(dirname "$cache_deb")"
    install -m 0644 "$built_deb" "$cache_deb"
    deb="$cache_deb"
  fi
fi
[[ -f "$deb" ]] || { echo "full keen-pbr Debian package not found: $deb" >&2; exit 2; }
deb="$(realpath "$deb")"
for command in qemu-system-x86_64 qemu-img mkfs.vfat mcopy mtype wget timeout; do
  command -v "$command" >/dev/null || { echo "QEMU integration tests require $command" >&2; exit 2; }
done

mkdir -p "$cache_root"
if [[ ! -f "$base_image" ]]; then
  wget -O "$base_image.tmp" "$image_url"
  mv "$base_image.tmp" "$base_image"
fi

for current_backend in "${backends[@]}"; do
  workdir="$(mktemp -d "$cache_root/run-${current_backend}.XXXXXX")"
  keep_workdir=0
  cleanup() { [[ "$keep_workdir" == 1 ]] || rm -rf "$workdir"; }
  trap cleanup EXIT
  overlay="$workdir/overlay.qcow2"
  seed="$workdir/seed.img"
  serial_log="$workdir/serial.log"
  payload="$workdir/payload.img"
  payload_dir="$workdir/payload"
  metadata="$workdir/meta-data"
  userdata="$workdir/user-data"
  printf 'instance-id: keen-pbr-%s\nlocal-hostname: keen-pbr-it\n' "$current_backend" >"$metadata"
  printf '%s\n' \
    '#cloud-config' \
    'users:' \
    '  - name: root' \
    '    lock_passwd: true' \
    'ssh_pwauth: false' \
    'runcmd:' \
    "  - [ bash, -c, 'mkdir -p /mnt/payload && mount -o ro /dev/vdc /mnt/payload && exec bash /mnt/payload/guest-run.sh $current_backend' ]" >"$userdata"
  mkfs.vfat -C -n CIDATA "$seed" 16384 >/dev/null
  mcopy -i "$seed" "$metadata" ::meta-data
  mcopy -i "$seed" "$userdata" ::user-data
  mkdir -p "$payload_dir/tests/integration"
  install -m 0644 "$deb" "$payload_dir/keen-pbr.deb"
  install -m 0755 "$repo_root/tests/integration/vm/guest-run.sh" "$payload_dir/guest-run.sh"
  cp -a "$repo_root/tests/integration/container" "$payload_dir/tests/integration/"
  mkfs.vfat -C -n KPBRDATA "$payload" 786432 >/dev/null
  mcopy -s -i "$payload" "$payload_dir"/* ::
  qemu-img create -f qcow2 -F qcow2 -b "$base_image" "$overlay" >/dev/null
  set +e
  timeout 12m qemu-system-x86_64 \
    -machine accel=kvm:tcg -m 1536 -smp 2 -display none -serial "file:$serial_log" -no-reboot \
    -drive "file=$overlay,if=virtio,format=qcow2" \
    -drive "file=$seed,if=virtio,format=raw" \
    -drive "file=$payload,if=virtio,format=raw,readonly=on" \
    -netdev user,id=net0 -device virtio-net-pci,netdev=net0
  qemu_status=$?
  set -e
  result="$(mtype -i "$seed" ::result 2>/dev/null | tr -d '[:space:]' || true)"
  if [[ "$qemu_status" != 0 || "$result" != 0 ]]; then
    keep_workdir=1
    echo "QEMU integration test failed (qemu=$qemu_status guest=${result:-missing})" >&2
    tr -d '\r' <"$serial_log" | \
      sed -E 's/^\[[[:space:]]*[0-9.]+\][[:space:]]+cloud-init\[[0-9]+\]:[[:space:]]*//' >&2 || true
    echo "QEMU integration diagnostics retained in $workdir" >&2
    exit 1
  fi
  rm -rf "$workdir"
  trap - EXIT
done
