#!/usr/bin/env bash

# Keep this list in one place: it covers launcher, sandbox, topology,
# fixtures, service control, shims, and the active integration cases.
harness_required_commands() {
  printf '%s\n' \
    awk bash cat chmod cp curl cut conntrack dirname dig dnsmasq env findmnt flock \
    grep hostname id ip ip6tables ip6tables-restore ip6tables-save ipset iptables \
    iptables-restore iptables-save ln mkdir mount mv nft nohup pkill ps python3 \
    readlink realpath rm sed seq setpriv setsid sh sleep sort stat sysctl \
    tail unshare
}

harness_required_shims() {
  printf '%s\n' journalctl modprobe ssh systemctl
}

check_harness_requirements() {
  local shim_dir=${1:-} command shim
  local -a missing=()

  while IFS= read -r command; do
    command -v "$command" >/dev/null 2>&1 || missing+=("$command")
  done < <(harness_required_commands)

  if [[ -n "$shim_dir" ]]; then
    while IFS= read -r shim; do
      [[ -x "$shim_dir/$shim" ]] || missing+=("$shim (bundled shim)")
    done < <(harness_required_shims)
  fi

  ((${#missing[@]} == 0)) && return 0

  local missing_csv
  printf -v missing_csv '%s,' "${missing[@]}"
  missing_csv=${missing_csv%,}
  printf 'KPBR_IT_END backend=harness status=error message=missing_executables=%s\n' \
    "$missing_csv" >&2
  printf 'Rootless integration prerequisites are missing:\n' >&2
  printf '  %s\n' "${missing[@]}" >&2
  printf 'Arch/CachyOS: sudo pacman -S --needed iproute2 procps-ng nftables '\
'iptables-nft ipset conntrack-tools dnsmasq bind curl python util-linux '\
'coreutils gawk grep sed bash\n' >&2
  printf 'Other distributions: install packages providing the listed executables; '\
'package names vary by distribution.\n' >&2
  return 2
}
