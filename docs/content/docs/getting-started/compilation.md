---
title: Build from Source
weight: 3
---

This page covers building keen-pbr3 from source for native hosts and embedded router targets.

## Prerequisites

| Requirement | Version |
|---|---|
| C++ compiler | C++20 support (GCC 10+, Clang 12+) |
| Meson | 0.60+ |
| Ninja | any recent |
| Conan | 2.x (`pip install "conan>=2.0,<3.0"`) |
| pkg-config | any |

## Dependencies

Dependencies are managed by Conan and downloaded automatically during the build.

| Library | Purpose |
|---|---|
| libcurl | Downloading remote lists |
| nlohmann_json | JSON parsing |
| libnl | Netlink socket communication (routing/rules) |
| mbedtls | TLS support for HTTPS list downloads |
| cpp-httplib | Embedded HTTP API server |

## Build

A `Makefile` wraps all build steps:

```bash
# Clone the repository
git clone https://github.com/maksimkurb/keen-pbr3.git
cd keen-pbr3

# Build (runs Conan + Meson + Ninja under the hood)
make

# Run tests
make test

# Clean build artifacts
make clean
```

Step-by-step targets are also available:

```bash
make deps     # conan install → build/
make setup    # meson setup
make build    # meson compile
```

The binary is produced at `build/keen-pbr3`.

## Build Options

Pass options during `meson setup`:

| Option | Default | Description |
|---|---|---|
| `with_api` | `true` | Build with embedded HTTP API server |
| `firewall_backend` | `auto` | `auto`, `nftables`, or `iptables` |

Example:

```bash
meson setup build --native-file=build/conan_meson_native.ini \
  -Dwith_api=false -Dfirewall_backend=iptables
```

## Cross-Compilation

Use the provided Docker setup to cross-compile for router targets without setting up a local toolchain:

```bash
# Build for all supported architectures
make docker-all

# Build for a specific architecture
make docker-mips-le-keenetic
```

Or run Docker manually:

```bash
docker build -f docker/Dockerfile.openwrt -t keen-pbr3-builder .
docker run --rm -v "$(pwd)/dist:/src/dist" keen-pbr3-builder <arch>
```

### Supported Architectures

| Architecture | Profile | Target |
|---|---|---|
| `mips-be-openwrt` | MIPS big-endian | OpenWRT routers |
| `mips-le-openwrt` | MIPS little-endian | OpenWRT routers |
| `arm-openwrt` | ARMv7hf | OpenWRT routers |
| `aarch64-openwrt` | AArch64 (ARMv8) | OpenWRT routers |
| `x86_64-openwrt` | x86_64 | OpenWRT routers |
| `mips-le-keenetic` | MIPS little-endian | Keenetic routers |

Output binaries are placed in `dist/<arch>/keen-pbr3`. Cross-compiled binaries are statically linked.

## Deployment to Router

Copy the binary and a config file to your router:

```bash
# Keenetic example
scp dist/mips-le-keenetic/keen-pbr3 root@192.168.1.1:/opt/sbin/keen-pbr3
scp config.json root@192.168.1.1:/etc/keen-pbr3/config.json

# OpenWRT example
scp dist/mips-le-openwrt/keen-pbr3 root@192.168.1.1:/usr/sbin/keen-pbr3
scp config.json root@192.168.1.1:/etc/keen-pbr3/config.json
```

Run on the router:

```bash
/opt/sbin/keen-pbr3 --config /etc/keen-pbr3/config.json -d
```
