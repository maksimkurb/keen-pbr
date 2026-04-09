---
title: Build from Source
weight: 1
aliases:
  - /docs/getting-started/compilation/
---

This page is for developers and package maintainers who want to build keen-pbr from source.

## Prerequisites

| Requirement | Version |
|---|---|
| C++ compiler | C++17 support (GCC 8.4+, Clang 9+) |
| CMake | 3.14+ |
| Make | any |
| pkg-config | any |
| Docker (for cross-compilation) | any |

## Dependencies

Dependencies are bundled as git submodules or resolved from system packages during the CMake build.

| Library | Purpose |
|---|---|
| libcurl | Downloading remote lists |
| nlohmann_json | JSON parsing |
| libnl | Netlink socket communication (routing/rules) |
| fmt | C++17 formatting polyfill |
| cpp-httplib | Embedded HTTP API server |

## Build

A `Makefile` wraps all build steps:

```bash {filename="bash"}
# Clone the repository
git clone https://github.com/maksimkurb/keen-pbr.git
cd keen-pbr

# Build
make

# Run tests
make test

# Clean build artifacts
make clean
```

Step-by-step targets are also available:

```bash {filename="bash"}
make setup    # cmake -S . -B cmake-build
make build    # cmake --build cmake-build
```

The binary is produced at `cmake-build/keen-pbr`.

Run `make help` to see all available build and packaging targets.

## Build Options

Pass options during `cmake` configure:

| Option | Default | Description |
|---|---|---|
| `WITH_API` | `ON` | Build with embedded HTTP API server |

Example:

```bash {filename="bash"}
cmake -S . -B cmake-build -DWITH_API=OFF
```

## Debian / Ubuntu Packages

For Debian/Ubuntu targets, use the Docker-backed package build targets from the root `Makefile`.

Build Debian packages from the repository root:

```bash {filename="bash"}
make deb-packages
```

Use an explicit Debian release label inside `build/packages/`:

```bash {filename="bash"}
make deb-packages DEBIAN_VERSION=bookworm
```

Rebuild the reusable Debian builder image explicitly:

```bash {filename="bash"}
make debian-builder-image
```

Artifacts are written to `build/packages/`:

- `build/packages/keen-pbr_<version>_debian_amd64.deb`
- `build/packages/keen-pbr-headless_<version>_debian_amd64.deb`

The Debian packaging flow:

- builds the frontend with `bun` for the full package
- compiles dedicated native binaries with Debian install paths under `/etc/keen-pbr` and `/usr/share/keen-pbr/frontend`
- installs a `systemd` unit at `/lib/systemd/system/keen-pbr.service`
- does not auto-enable or auto-start the service during package installation

## Router Package Builds

For router targets, use the Docker-backed package build targets from the root `Makefile`. They mirror the GitHub Actions workflows and write normalized artifacts into `build/packages/`.

### Entware (for Keenetic and Netcraze routers)

Build an Entware package:

```bash {filename="bash"}
make keenetic-packages KEENETIC_CONFIG=mipsel-3.4 KEENETIC_VERSION=current
```

Parameters:

| Variable | Required | Example | Description |
|---|---|---|---|
| `KEENETIC_VERSION` | yes | `current` | Keenetic channel version used in the `build/packages` layout. |
| `KEENETIC_CONFIG` | yes | `mipsel-3.4` | Entware builder architecture tag.<br>Supported values: <br>`aarch64-3.10`, <br>`mips-3.4`, <br>`mipsel-3.4`, <br>`armv7-3.2`, <br>`x64-3.2`. |

Behavior:

- Uses the prebuilt `ghcr.io/maksimkurb/entware-builder:<config>` image
- Mounts the repo into the container at `/workspace`
- Builds and collects artifacts into `build/packages/`

### OpenWrt

List available targets first:

```bash {filename="bash"}
make list-openwrt-targets OPENWRT_VERSION=24.10.4
make list-openwrt-targets OPENWRT_VERSION=24.10.4 OPENWRT_TARGET=mediatek
make list-openwrt-targets OPENWRT_VERSION=24.10.4 OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic
```

Build an OpenWrt package:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic
```

Or with an explicit release:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic

# another common example
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_TARGET=rockchip OPENWRT_SUBTARGET=armv8
```

Rebuild the reusable OpenWrt builder image explicitly:

```bash {filename="bash"}
make openwrt-builder-image
```

Parameters:

| Variable | Required | Default | Example | Description |
|---|---|---|---|---|
| `OPENWRT_VERSION` | yes | - | `24.10.4` | OpenWrt release used for SDK discovery. |
| `OPENWRT_TARGET` | yes | - | `mediatek` | OpenWrt target name. |
| `OPENWRT_SUBTARGET` | yes | - | `filogic` | OpenWrt subtarget name. |

Behavior:

- Builds `docker/Dockerfile.openwrt-builder`
- Mounts the repo into the container at `/workspace`
- Downloads and extracts the matching OpenWrt SDK into the mounted SDK cache on first run
- Reuses the SDK cache on later runs
- Copies resulting `.ipk` / `.apk` artifacts into `build/packages/`

### Notes

- The repo is bind-mounted into `/workspace`, so source and packaging-script edits are picked up on the next build.
- OpenWrt SDK contents are cached outside the container and reused across runs.

## Deployment to Router

Copy a built package and a config file to your router:

```bash {filename="bash"}
# Keenetic example
scp build/packages/keenetic/<keenetic_version>/<pkg_arch>/keen-pbr_<version>_keenetic_<config>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_keenetic_<config>.ipk

# OpenWrt .ipk example
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>_<pkg_arch>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>_<pkg_arch>.ipk

# OpenWrt .apk example
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>_<pkg_arch>.apk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 apk add --allow-untrusted /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>_<pkg_arch>.apk

```

Run on the router:

```bash {filename="bash"}
# Keenetic
/opt/etc/init.d/S80keen-pbr start

# OpenWrt
service keen-pbr start
```
