#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: build-package.sh --variant <full|headless> --build-dir <path> --output-dir <path> --version <version> [--arch <arch>] [--frontend-dist <path>]
EOF
}

variant=""
build_dir=""
output_dir=""
version=""
arch="$(dpkg-architecture -qDEB_HOST_ARCH)"
frontend_dist=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --variant)
      variant="${2:-}"
      shift 2
      ;;
    --build-dir)
      build_dir="${2:-}"
      shift 2
      ;;
    --output-dir)
      output_dir="${2:-}"
      shift 2
      ;;
    --version)
      version="${2:-}"
      shift 2
      ;;
    --arch)
      arch="${2:-}"
      shift 2
      ;;
    --frontend-dist)
      frontend_dist="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "$variant" || -z "$build_dir" || -z "$output_dir" || -z "$version" ]]; then
  usage >&2
  exit 1
fi

case "$variant" in
  full)
    package_name="keen-pbr"
    config_template="config.full.example.json"
    control_template="control.full.in"
    include_frontend=1
    ;;
  headless)
    package_name="keen-pbr-headless"
    config_template="config.headless.example.json"
    control_template="control.headless.in"
    include_frontend=0
    ;;
  *)
    echo "Unsupported variant: $variant" >&2
    exit 1
    ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
files_dir="${script_dir}/files"

build_dir="$(cd "$build_dir" && pwd)"
output_dir="$(mkdir -p "$output_dir" && cd "$output_dir" && pwd)"

if [[ ! -x "${build_dir}/keen-pbr" ]]; then
  echo "Expected built binary at ${build_dir}/keen-pbr" >&2
  exit 1
fi

if [[ "$include_frontend" -eq 1 ]]; then
  if [[ -z "$frontend_dist" ]]; then
    echo "--frontend-dist is required for the full package" >&2
    exit 1
  fi
  frontend_dist="$(cd "$frontend_dist" && pwd)"
  if [[ ! -d "$frontend_dist" ]]; then
    echo "Frontend dist directory not found: $frontend_dist" >&2
    exit 1
  fi
fi

pkg_root="${repo_root}/build/debian/${package_name}"
pkg_debian_dir="${pkg_root}/DEBIAN"
rm -rf "$pkg_root"

install -d \
  "${pkg_debian_dir}" \
  "${pkg_root}/etc/keen-pbr" \
  "${pkg_root}/lib/systemd/system" \
  "${pkg_root}/usr/lib/keen-pbr" \
  "${pkg_root}/usr/sbin" \
  "${pkg_root}/var/cache/keen-pbr"

install -m 0755 "${build_dir}/keen-pbr" "${pkg_root}/usr/sbin/keen-pbr"
install -m 0755 "${files_dir}/usr/lib/keen-pbr/dnsmasq.sh" "${pkg_root}/usr/lib/keen-pbr/dnsmasq.sh"
install -m 0644 "${files_dir}/lib/systemd/system/keen-pbr.service" "${pkg_root}/lib/systemd/system/keen-pbr.service"
install -m 0644 "${repo_root}/packages/common/${config_template}" "${pkg_root}/etc/keen-pbr/config.json"
install -m 0644 "${repo_root}/packages/common/local.lst" "${pkg_root}/etc/keen-pbr/local.lst"

if [[ "$include_frontend" -eq 1 ]]; then
  install -d "${pkg_root}/usr/share/keen-pbr/frontend"
  cp -a "${frontend_dist}/." "${pkg_root}/usr/share/keen-pbr/frontend/"
  chmod -R a=rX,u+w "${pkg_root}/usr/share/keen-pbr/frontend"
fi

install -m 0755 "${files_dir}/DEBIAN/postinst" "${pkg_debian_dir}/postinst"
install -m 0755 "${files_dir}/DEBIAN/prerm" "${pkg_debian_dir}/prerm"
install -m 0755 "${files_dir}/DEBIAN/postrm" "${pkg_debian_dir}/postrm"

cat > "${pkg_debian_dir}/conffiles" <<'EOF'
/etc/keen-pbr/config.json
/etc/keen-pbr/local.lst
EOF

tmp_debian_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_debian_dir}"' EXIT
mkdir -p "${tmp_debian_dir}/debian"
cat > "${tmp_debian_dir}/debian/control" <<EOF
Source: keen-pbr
Section: net
Priority: optional
Maintainer: keen-pbr maintainers <noreply@keen-pbr.local>
Standards-Version: 4.6.2

Package: ${package_name}
Architecture: ${arch}
Depends: \${shlibs:Depends}
Description: temporary control file for dependency resolution
EOF

deps="$(
  cd "${tmp_debian_dir}"
  dpkg-shlibdeps -O "${pkg_root}/usr/sbin/keen-pbr" | sed -n 's/^shlibs:Depends=//p'
)"
if [[ -z "$deps" ]]; then
  echo "Failed to determine shared library dependencies for ${package_name}" >&2
  exit 1
fi

installed_size="$(du -sk "${pkg_root}" | awk '{print $1}')"
sed \
  -e "s|@VERSION@|${version}|g" \
  -e "s|@ARCHITECTURE@|${arch}|g" \
  -e "s|@INSTALLED_SIZE@|${installed_size}|g" \
  -e "s|@DEPENDS@|${deps}|g" \
  "${script_dir}/${control_template}" > "${pkg_debian_dir}/control"

dpkg-deb --root-owner-group --build "${pkg_root}" "${output_dir}/${package_name}_${version}_${arch}.deb"
