---
title: Build from Source
weight: 3
---

This page covers building keen-pbr from source for native hosts and embedded router targets.

## Prerequisites

| Requirement | Version |
|---|---|
| C++ compiler | C++17 support (GCC 8.4+, Clang 9+) |
| pkg-config | any |

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

```bash
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

```bash
make setup    # cmake -S . -B cmake-build
make build    # cmake --build cmake-build
```

The binary is produced at `cmake-build/keen-pbr`.

## Build Options

Pass options during `cmake` configure:

| Option | Default | Description |
|---|---|---|
| `WITH_API` | `ON` | Build with embedded HTTP API server |

Example:

```bash
cmake -S . -B cmake-build -DWITH_API=OFF
```

## Router Package Builds

For router targets, use the Docker-backed package build targets from the root `Makefile`. They mirror the GitHub Actions workflows and write normalized artifacts into `build/packages/`.

### Keenetic / Entware

Build a Keenetic package:

```bash
make keenetic-packages KEENETIC_CONFIG=mipsel-3.4
```

Parameters:

| Variable | Required | Example | Description |
|---|---|---|---|
| `KEENETIC_CONFIG` | yes | `mipsel-3.4` | Entware builder architecture tag. |

Supported `KEENETIC_CONFIG` values:

- `aarch64-3.10`
- `mips-3.4`
- `mipsel-3.4`
- `x64-3.2`
- `armv7-3.2`

Behavior:

- Uses the prebuilt `ghcr.io/maksimkurb/entware-builder:<config>` image
- Mounts the repo into the container at `/workspace`
- Builds and collects artifacts into `build/packages/`

### OpenWrt

List available targets first:

```bash
make list-openwrt-targets
make list-openwrt-targets OPENWRT_VERSION=24.10.4
make list-openwrt-targets OPENWRT_TARGET=mediatek
make list-openwrt-targets OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic
```

Build an OpenWrt package:

```bash
make openwrt-packages OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic
```

Or with an explicit release:

```bash
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic
```

Parameters:

| Variable | Required | Default | Example | Description |
|---|---|---|---|---|
| `OPENWRT_VERSION` | no | `24.10.4` | `24.10.4` | OpenWrt release used for SDK discovery. |
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

```bash
# Keenetic example
scp build/packages/keen-pbr_<version>_keenetic_<arch>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_keenetic_<arch>.ipk
scp config.json root@192.168.1.1:/opt/etc/keen-pbr/config.json

# OpenWRT example
scp build/packages/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>.ipk
scp config.json root@192.168.1.1:/etc/keen-pbr/config.json
```

Run on the router:

```bash
/opt/etc/init.d/S80keen-pbr start
```
