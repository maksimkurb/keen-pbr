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

For router targets, use the Docker-backed package build targets from the root `Makefile`. They mirror the GitHub Actions workflows and keep a persistent builder container per target, so repeat builds reuse the existing SDK / Entware workspace instead of rebuilding it from scratch.

Built artifacts are copied into `release_files/`.

### Keenetic / Entware

Build a Keenetic package:

```bash
make KEENETIC_ARCH=mipsel-3.4 docker-build-keenetic
```

Parameters:

| Variable | Required | Example | Description |
|---|---|---|---|
| `KEENETIC_ARCH` | yes | `mipsel-3.4` | Entware builder architecture tag. |

Supported `KEENETIC_ARCH` values:

- `aarch64-3.10`
- `mips-3.4`
- `mipsel-3.4`
- `x64-3.2`
- `armv7-3.2`

Behavior:

- Builds `docker/Dockerfile.keenetic-builder`
- Creates a persistent container named `keen-pbr-keenetic-<arch>` on first run
- Mounts the repo into the container at `/src`
- Reuses that same container on later runs
- Rebuilds only the `keen-pbr` package
- Copies the resulting `.ipk` into `release_files/keen-pbr_<version>_keenetic_<arch>.ipk`

Cleanup:

```bash
make KEENETIC_ARCH=mipsel-3.4 docker-clean-keenetic
```

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
make OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic docker-build-openwrt
```

Or with an explicit release:

```bash
make OPENWRT_VERSION=24.10.4 OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic docker-build-openwrt
```

Parameters:

| Variable | Required | Default | Example | Description |
|---|---|---|---|---|
| `OPENWRT_VERSION` | no | `24.10.4` | `24.10.4` | OpenWrt release used for SDK discovery. |
| `OPENWRT_TARGET` | yes | - | `mediatek` | OpenWrt target name. |
| `OPENWRT_SUBTARGET` | yes | - | `filogic` | OpenWrt subtarget name. |

Behavior:

- Builds `docker/Dockerfile.openwrt-builder`
- Creates a persistent container named `keen-pbr-openwrt-<version>-<target>-<subtarget>` on first run
- Mounts the repo into the container at `/src`
- Downloads and extracts the matching OpenWrt SDK inside the persistent container on first run
- Reuses that same container and SDK on later runs
- Rebuilds only the `keen-pbr` package
- Copies resulting `.ipk` / `.apk` artifacts into `release_files/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>.<ext>`

Cleanup:

```bash
make OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic docker-clean-openwrt
```

### Notes

- Build containers are stopped after each build, not removed.
- If you change a builder Dockerfile, remove the existing container before rebuilding so it can be recreated from the updated image.
- The repo is mounted into `/src`, so source and packaging-script edits are picked up on the next build without recreating the container.

## Deployment to Router

Copy a built package and a config file to your router:

```bash
# Keenetic example
scp release_files/keen-pbr_<version>_keenetic_<arch>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_keenetic_<arch>.ipk
scp config.json root@192.168.1.1:/opt/etc/keen-pbr/config.json

# OpenWRT example
scp release_files/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>.ipk
scp config.json root@192.168.1.1:/etc/keen-pbr/config.json
```

Run on the router:

```bash
/opt/etc/init.d/S80keen-pbr start
```
