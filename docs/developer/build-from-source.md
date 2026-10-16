# Build from Source

This page is for developers and package maintainers who want to build keen-pbr from source. If you just need a ready-made package, see [installation]({{< relref "/docs/getting-started" >}}).

## Prerequisites

| Requirement | Version |
|---|---|
| C++ compiler | C++17 support (GCC 8.4+, Clang 9+) |
| CMake | 3.14+ |
| Make | any |
| pkg-config | any |
| git | any |
| Docker with Buildx (for package builds) | any |

## Dependencies

Some libraries are bundled in the repository as git submodules (the `third_party` folder); the rest come from system packages during the CMake build. For a host build, install the development headers of the system libraries (on Debian and Ubuntu: `libcurl4-openssl-dev`, `libnl-3-dev`, `libnl-route-3-dev`, `zlib1g-dev`).

| Library | Source | Purpose |
|---|---|---|
| libcurl | system | Downloading remote lists |
| libnl-3, libnl-route-3 | system | Routes and rules through Netlink |
| zlib | system | Needed by the built-in HTTP server (`WITH_API=ON`) |
| libresolv | part of libc | DNS resolution |
| libunwind | system | Call stacks on crashes. Debian builds need it by default. OpenWrt and Keenetic builds work without it and show only registers on a crash. `KEEN_PBR_USE_LIBUNWIND` controls whether CMake looks for it. |
| nlohmann_json | submodule | JSON parsing |
| fmt | submodule | String formatting |
| Monocypher | submodule | Cryptography (password hashing) |
| cpp-httplib | submodule | Built-in HTTP server when `WITH_API=ON` |

## Build

A `Makefile` wraps all build steps:

```bash {filename="bash"}
# Clone the repository with submodules
git clone --recurse-submodules https://github.com/maksimkurb/keen-pbr.git
cd keen-pbr

# Build
make

# Run tests (needs python3 and sh; dnsmasq is optional)
make test

# Clean build artifacts
make clean
```

Step-by-step targets are also available:

```bash {filename="bash"}
make setup    # cmake -S . -B cmake-build-gcc
make build    # cmake --build cmake-build-gcc
```

The binary is produced at `cmake-build-gcc/keen-pbr`. The Clang targets (`make clang-build`) build into `cmake-build-clang`.

Run `make help` to see all build and packaging targets.

## Build Options

Pass options when configuring with `cmake`:

| Option | Default | Description |
|---|---|---|
| `WITH_API` | `ON` | Build with the built-in HTTP server (REST API and web interface) |
| `USE_KEENETIC_API` | `OFF` | Enable the Keenetic integration (DNS server type `keenetic`) |
| `KEEN_PBR_PLATFORM_KEENETIC` | `OFF` | Build for KeeneticOS: iptables only, no balancing |
| `KEEN_PBR_USE_LIBUNWIND` | `ON` | Look for and use `libunwind` for crash diagnostics. Debian builds require it when enabled; OpenWrt and Keenetic use register-only diagnostics when it is missing or disabled. |
| `BUILD_TESTS` | `OFF` | Build unit tests (`make test` turns it on itself) |

Example: build without the HTTP server (like the `keen-pbr-headless` package):

```bash {filename="bash"}
cmake -S . -B cmake-build-gcc -DWITH_API=OFF
cmake --build cmake-build-gcc
```

## Debian / Ubuntu Packages

Debian and Ubuntu packages are built in Docker with targets from the root `Makefile`. You need Docker and the Buildx plugin. Run from the repository root:

```bash {filename="bash"}
make deb-packages
```

You can set the Debian release (`bookworm` or `trixie`) and the architecture (`amd64` or `arm64`) explicitly. The defaults are `bookworm` and `amd64`:

```bash {filename="bash"}
# AMD64
make deb-packages DEBIAN_VERSION=bookworm DEBIAN_ARCH=amd64

# ARM64 (Docker uses linux/arm64 automatically)
make deb-packages DEBIAN_VERSION=bookworm DEBIAN_ARCH=arm64
```

To recreate the Debian builder image:

```bash {filename="bash"}
make debian-builder-image
```

The finished packages (full and `headless`) appear in `build/packages/`:

- `build/packages/debian/<debian-version>/<architecture>/keen-pbr_<version>_debian_<architecture>.deb`
- `build/packages/debian/<debian-version>/<architecture>/keen-pbr-headless_<version>_debian_<architecture>.deb`

The published repository also contains packages for Ubuntu: Ubuntu noble (24.04) uses the Debian bookworm packages, and Ubuntu resolute (26.04) uses the Debian trixie packages.

For the full package, the web interface is built with `bun`. Installing the package enables and starts the `keen-pbr` service (a `systemd` unit); the configuration lives in `/etc/keen-pbr`.

## Router Package Builds

Router packages are also built in Docker with targets from the root `Makefile`. They repeat the GitHub Actions build, and the finished packages appear in `build/packages/`.

### Entware (for Keenetic and Netcraze routers)

Build an {{< term "Entware" >}} package:

```bash {filename="bash"}
make keenetic-packages KEENETIC_CONFIG=mipsel-3.4 KEENETIC_VERSION=current
```

Parameters:

| Variable | Required | Example | Description |
|---|---|---|---|
| `KEENETIC_VERSION` | yes | `current` | Keenetic firmware channel: the name of the subfolder in `build/packages/keenetic/`. |
| `KEENETIC_CONFIG` | yes | `mipsel-3.4` | Entware builder architecture tag.<br>Supported values: <br>`aarch64-3.10`, <br>`mips-3.4`, <br>`mipsel-3.4`, <br>`armv7-3.2`, <br>`x64-3.2`. |

Behavior:

- It uses the prebuilt `ghcr.io/maksimkurb/entware-builder:<config>` image.
- The repository is mounted into the container at `/workspace`.
- Packages (`keen-pbr` and `keen-pbr-headless`) are copied to `build/packages/keenetic/<version>/<architecture>/`, for example `keen-pbr_<version>_keenetic_mipsel-3.4.ipk`.

### OpenWrt

List available architectures first:

```bash {filename="bash"}
make list-openwrt-architectures OPENWRT_VERSION=24.10.4
```

Build an OpenWrt package:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_ARCHITECTURE=aarch64_cortex-a53
```

Another example:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=25.12.2 OPENWRT_ARCHITECTURE=x86_64
```

Parameters:

| Variable | Required | Default | Example | Description |
|---|---|---|---|---|
| `OPENWRT_VERSION` | yes | - | `24.10.4` | OpenWrt version (selects the SDK image). |
| `OPENWRT_ARCHITECTURE` | yes | - | `aarch64_cortex-a53` | OpenWrt package architecture. |

Behavior:

- It uses the official SDK image `ghcr.io/openwrt/sdk:<architecture>-<version>`.
- The repository is mounted into the container at `/workspace`, so source edits are picked up on the next build.
- The `.ipk` / `.apk` packages are copied to `build/packages/openwrt/<version>/<architecture>/`.

## Deployment to Router

Copy the built package to your router and install it (replace `192.168.1.1` with your router's address):

```bash {filename="bash"}
# Keenetic example
scp build/packages/keenetic/<keenetic_version>/<pkg_arch>/keen-pbr_<version>_keenetic_<config>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_keenetic_<config>.ipk

# OpenWrt .ipk example
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.ipk

# OpenWrt .apk example
# the .apk file keeps the name the OpenWrt SDK gave it
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr-<version>.apk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 apk add --allow-untrusted /tmp/keen-pbr-<version>.apk
```

After installation the service is usually already running. If not, start it on the router:

```bash {filename="bash"}
# Keenetic
/opt/etc/init.d/S80keen-pbr start

# OpenWrt
service keen-pbr start
```

