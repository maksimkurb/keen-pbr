# keen-pbr

Policy-based routing daemon for OpenWRT and Keenetic routers. Routes traffic selectively by IP addresses, subnets, and domains with interface failover, health checks, and optional REST API.

Written in C++17, designed for embedded targets (MIPS, ARM, AArch64, x86_64) with minimal binary size.

## Features

- **Selective routing** — route traffic through specific interfaces based on IP/CIDR/domain lists
- **Multiple outbound types** — interface (with optional gateway), routing table, blackhole, ignore, urltest (auto-select by latency)
- **Failover chains** — ordered list of outbounds; first healthy one wins
- **Health monitoring** — ICMP ping-based checks with circuit breaker to prevent oscillation
- **Dual firewall backend** — auto-detects iptables/ipset or nftables
- **DNS integration** — generates dnsmasq config for domain-based routing via ipsets
- **Remote lists** — download IP/domain lists from URLs with disk caching for offline startup
- **REST API** (optional, compile-time) — service health, routing verification, config management, routing test, DNS test, and reload endpoints
- **Signal handling** — `SIGHUP` triggers full reload (re-download lists, re-apply rules); `SIGUSR1` re-verifies routing tables and triggers URL tests; `SIGTERM`/`SIGINT` for graceful shutdown

## Prerequisites

- **C++17 compiler** (GCC 8.4+ or Clang 9+)
- **CMake** (>= 3.14)
- **Make**
- **pkg-config**

### Dependencies

| Library | Version | Purpose |
|---|---|---|
| libcurl | 8.5.0 | HTTP client for list downloads |
| nlohmann_json | 3.11.3 | JSON configuration parsing |
| libnl | 3.8.0 | Netlink route/rule management |
| fmt | vendored | C++17 formatting polyfill |
| cpp-httplib | 0.31.0 | REST API server (optional) |

## Building

A `Makefile` wraps all build steps. Run `make help` to see available targets.

### Quick start

```bash
make          # Native build
make clean    # Remove build directory
```

The binary will be at `cmake-build/keen-pbr`.

### Native build (step by step)

```bash
make setup    # cmake -S . -B cmake-build
make build    # cmake --build cmake-build
```

### Build options

| Option | Type | Default | Description |
|---|---|---|---|
| `WITH_API` | boolean | `ON` | Build with REST API support (cpp-httplib) |

Pass options during `cmake` configure:

```bash
cmake -S . -B cmake-build -DWITH_API=OFF
```

### Router package builds (Docker)

For router package builds, use the Docker-backed `Makefile` targets. These mirror the GitHub Actions flows and keep a persistent builder container per target so repeat builds reuse the existing SDK / Entware workspace instead of rebuilding it from scratch.

Built artifacts are copied into `release_files/`.

#### Keenetic package build

Build a Keenetic/Entware package:

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
- Reuses the same container on later runs and recompiles only the `keen-pbr` package
- Copies the resulting `.ipk` into `release_files/keen-pbr_<version>_keenetic_<arch>.ipk`

Cleanup:

```bash
make KEENETIC_ARCH=mipsel-3.4 docker-clean-keenetic
```

#### OpenWrt package build

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
- Reuses that same container and SDK on later runs, rebuilding only the `keen-pbr` package
- Copies the resulting `.ipk` / `.apk` artifacts into `release_files/keen-pbr_<version>_openwrt_<openwrt_version>_<target>_<subtarget>.<ext>`

Cleanup:

```bash
make OPENWRT_TARGET=mediatek OPENWRT_SUBTARGET=filogic docker-clean-openwrt
```

#### Notes

- Build containers are stopped after each build, not removed.
- If you change the Dockerfile for a builder image, remove the existing container first so it can be recreated from the new image.
- The repo is mounted into `/src`, so changes to package scripts or source files are picked up on the next build without rebuilding the container itself.

## Configuration

keen-pbr reads a JSON config file whose default path is set at build time (`/etc/keen-pbr/config.json` on OpenWrt builds, `/opt/etc/keen-pbr/config.json` on Keenetic builds).

### Example config

```json
{
  "daemon": {
    "pid_file": "/var/run/keen-pbr.pid",
    "list_update_interval": "24h"
  },
  "api": {
    "enabled": true,
    "listen": "127.0.0.1:8080"
  },
  "outbounds": [
    {
      "type": "interface",
      "tag": "vpn",
      "interface": "tun0",
      "gateway": "10.8.0.1",
      "ping_target": "8.8.8.8",
      "ping_interval": "30s",
      "ping_timeout": "5s"
    },
    {
      "type": "interface",
      "tag": "wan",
      "interface": "eth0",
      "gateway": "192.168.1.1"
    },
    {
      "type": "blackhole",
      "tag": "block"
    }
  ],
  "lists": {
    "my-domains": {
      "url": "https://example.com/domains.txt",
      "domains": ["extra.example.com", "*.custom.org"]
    },
    "my-ips": {
      "url": "https://example.com/ips.txt",
      "ip_cidrs": ["10.0.0.0/8", "192.168.100.1"]
    },
    "local-list": {
      "file": "/etc/keen-pbr/local.txt"
    }
  },
  "route": {
    "rules": [
      {
        "list": ["my-domains", "my-ips"],
        "outbound": "vpn"
      },
      {
        "list": ["local-list"],
        "outbound": "direct"
      }
    ]
  },
  "dns": {
    "servers": [
      {
        "tag": "vpn-dns",
        "address": "8.8.8.8",
        "detour": "vpn"
      },
      {
        "tag": "local-dns",
        "address": "system"
      },
      {
        "tag": "doh-dns",
        "address": "https://dns.google/dns-query"
      },
      {
        "tag": "block-dns",
        "address": "rcode://refused"
      }
    ],
    "rules": [
      {
        "list": ["my-domains"],
        "server": "vpn-dns"
      }
    ],
    "fallback": "local-dns"
  }
}
```

### Duration format

Duration fields (`list_update_interval`, `ping_interval`, `ping_timeout`) accept strings with suffixes: `s` (seconds), `m` (minutes), `h` (hours). Examples: `"30s"`, `"5m"`, `"24h"`.

### List format

List files (remote or local) contain one entry per line. Supported entry types:

- IPv4 addresses: `192.168.1.1`
- IPv6 addresses: `2001:db8::1`
- CIDR subnets: `10.0.0.0/8`, `2001:db8::/32`
- Domain names: `example.com`
- Wildcard domains: `*.example.com` (matches subdomains and the base domain)
- Comments: lines starting with `#` are ignored
- Empty lines are skipped

### Route rules

Each route rule matches traffic against one or more lists and routes it through the specified outbound:

- `"outbound": "tag"` — route matched traffic to the named outbound

Rules are evaluated in order; first match wins.

## Usage

```bash
keen-pbr [options]
```

### Options

| Flag | Description |
|---|---|
| `--config <path>` | Path to JSON config file (default depends on the build target) |
| `-d` | Daemonize (run in background) |
| `--no-api` | Disable REST API at runtime |
| `--version`, `-v` | Show version and exit |
| `--help`, `-h` | Show help and exit |

### Signals

| Signal | Effect |
|---|---|
| `SIGHUP` | Full reload: re-download lists, re-apply firewall and routing rules |
| `SIGUSR1` | Re-verify routing tables and trigger immediate URL tests |
| `SIGTERM` / `SIGINT` | Graceful shutdown (cleanup routes, firewall, PID file) |

### REST API endpoints

Available when built with `WITH_API=ON` and not disabled with `--no-api`:

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/health/service` | Daemon version, status, and resolver config summary |
| `GET` | `/api/health/routing` | Verify live kernel routing and firewall state |
| `GET` | `/api/runtime/outbounds` | Live outbound and interface runtime state |
| `POST` | `/api/reload` | Trigger async full reload (re-download lists, re-apply rules) |
| `GET` | `/api/config` | Get current config (with draft indicator) |
| `POST` | `/api/config` | Validate and stage config in memory (no write, no reload) |
| `POST` | `/api/config/save` | Persist staged config to disk and apply it |
| `POST` | `/api/routing/test` | Test routing for an IP address or domain name |
| `GET` | `/api/dns/test` | Stream live DNS test queries as Server-Sent Events |

### Running on the router

```bash
# Copy the built package to the router
scp release_files/keen-pbr_<version>_keenetic_<arch>.ipk root@router:/tmp/

# Install it
opkg install /tmp/keen-pbr_<version>_keenetic_<arch>.ipk

# Copy config
scp config.json root@router:/opt/etc/keen-pbr/config.json

# Run as daemon
/opt/etc/init.d/S80keen-pbr start
```

### Dnsmasq integration

keen-pbr generates a dnsmasq config file at `/tmp/keen-pbr-dnsmasq.conf` with `ipset=` and `server=` directives for domain-based routing. Add this to your dnsmasq configuration:

```
conf-file=/tmp/keen-pbr-dnsmasq.conf
```

Then restart dnsmasq after keen-pbr starts.

## How it works

1. **Startup** — loads config, downloads/caches remote lists, reads local lists and inline entries
2. **Firewall setup** — creates ipsets/nft sets for IP/CIDR entries, adds mangle rules to mark matching packets with fwmarks
3. **Routing setup** — creates ip rules (fwmark → routing table) and routes (default via interface/gateway) in corresponding tables
4. **DNS setup** — generates dnsmasq config so resolved domain IPs get added to the appropriate ipsets at DNS resolution time
5. **Health monitoring** — periodically pings configured targets through their bound interfaces; circuit breaker prevents rapid failover
6. **Event loop** — epoll-based loop handles signals, timers, and optional API requests

## License

See [LICENSE](LICENSE) for details.
