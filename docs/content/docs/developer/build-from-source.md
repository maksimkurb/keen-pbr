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
| libunwind | Required build-time stack unwinding backend for crash diagnostics; linked statically by default when `libunwind.a` is available |
| fmt | C++17 formatting polyfill |
| cpptrace | Vendored crash diagnostics library |
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
| `KEEN_PBR_STATIC_LIBUNWIND` | `ON` | Prefer linking `libunwind` statically; set to `OFF` to force the shared library |

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

List available architectures first:

```bash {filename="bash"}
make list-openwrt-architectures OPENWRT_VERSION=24.10.4
```

Build an OpenWrt package:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_ARCHITECTURE=aarch64_cortex-a53
```

Or with an explicit release:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_ARCHITECTURE=aarch64_cortex-a53

# another common example
make openwrt-packages OPENWRT_VERSION=25.12.2 OPENWRT_ARCHITECTURE=x86_64
```

Parameters:

| Variable | Required | Default | Example | Description |
|---|---|---|---|---|
| `OPENWRT_VERSION` | yes | - | `24.10.4` | OpenWrt release used to select the SDK image. |
| `OPENWRT_ARCHITECTURE` | yes | - | `aarch64_cortex-a53` | OpenWrt package architecture and SDK image tag prefix. |

Behavior:

- Mounts the repo into the container at `/workspace`
- Uses the official SDK image `ghcr.io/openwrt/sdk:<architecture>-<version>`
- Copies resulting `.ipk` / `.apk` artifacts into `build/packages/`

### Notes

- The repo is bind-mounted into `/workspace`, so source and packaging-script edits are picked up on the next build.
- OpenWrt packages are built inside the SDK image that matches the requested version and architecture.

## Deployment to Router

Copy a built package and a config file to your router:

```bash {filename="bash"}
# Keenetic example
scp build/packages/keenetic/<keenetic_version>/<pkg_arch>/keen-pbr_<version>_keenetic_<config>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_keenetic_<config>.ipk

# OpenWrt .ipk example
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.ipk

# OpenWrt .apk example
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.apk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 apk add --allow-untrusted /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.apk

```

Run on the router:

```bash {filename="bash"}
# Keenetic
/opt/etc/init.d/S80keen-pbr start

# OpenWrt
service keen-pbr start
```
