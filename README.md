# keen-pbr3

Policy-based routing daemon for OpenWRT and Keenetic routers. Routes traffic selectively by IP addresses, subnets, and domains with interface failover, health checks, and optional REST API.

Written in C++20, designed for embedded targets (MIPS, ARM, AArch64, x86_64) with minimal binary size.

## Features

- **Selective routing** — route traffic through specific interfaces based on IP/CIDR/domain lists
- **Multiple outbound types** — interface (with optional gateway), routing table, blackhole
- **Failover chains** — ordered list of outbounds; first healthy one wins
- **Health monitoring** — ICMP ping-based checks with circuit breaker to prevent oscillation
- **Dual firewall backend** — auto-detects iptables/ipset or nftables
- **DNS integration** — generates dnsmasq config for domain-based routing via ipsets
- **Remote lists** — download IP/domain lists from URLs with disk caching for offline startup
- **REST API** (optional, compile-time) — status, health, and reload endpoints
- **Signal handling** — `SIGUSR1` triggers immediate list reload; `SIGTERM`/`SIGINT` for graceful shutdown

## Prerequisites

- **C++20 compiler** (GCC 10+ or Clang 12+)
- **Meson** (>= 0.56)
- **Ninja**
- **Conan 2.x** (`pip install "conan>=2.0,<3.0"`)
- **pkg-config**

### Dependencies (managed by Conan)

| Library | Version | Purpose |
|---|---|---|
| libcurl | 8.5.0 | HTTP client for list downloads |
| nlohmann_json | 3.11.3 | JSON configuration parsing |
| libnl | 3.8.0 | Netlink route/rule management |
| mbedtls | 3.5.0 | TLS backend for HTTPS |
| cpp-httplib | 0.14.0 | REST API server (optional) |

## Building

A `Makefile` wraps all build steps. Run `make help` to see available targets.

### Quick start

```bash
make          # Native build (deps + setup + compile)
make clean    # Remove build directory
```

The binary will be at `build/keen-pbr3`.

### Native build (step by step)

```bash
make deps     # conan install → build/
make setup    # meson setup
make build    # meson compile
```

### Build options

| Option | Type | Default | Description |
|---|---|---|---|
| `with_api` | boolean | `true` | Build with REST API support (cpp-httplib) |
| `firewall_backend` | combo | `auto` | Firewall backend: `auto`, `iptables`, or `nftables` |

Pass options during `meson setup`:

```bash
meson setup build --native-file=build/conan_meson_native.ini -Dwith_api=false
```

### Cross-compilation (Docker)

The easiest way to build for embedded targets. No local toolchain setup required.

```bash
make docker-all                  # Build for all architectures
make docker-mips-le-keenetic     # Build for a single architecture
```

Or manually:

```bash
docker build -f docker/Dockerfile.openwrt -t keen-pbr3-builder .
docker run --rm -v "$(pwd)/dist:/src/dist" keen-pbr3-builder <arch>
```

#### Supported architectures

| Architecture | Profile | Target |
|---|---|---|
| `mips-be-openwrt` | MIPS big-endian | OpenWRT routers |
| `mips-le-openwrt` | MIPS little-endian | OpenWRT routers |
| `arm-openwrt` | ARMv7hf | OpenWRT routers |
| `aarch64-openwrt` | AArch64 (ARMv8) | OpenWRT routers |
| `x86_64-openwrt` | x86_64 | OpenWRT routers |
| `mips-le-keenetic` | MIPS little-endian | Keenetic routers |

Output binaries go to `dist/<arch>/keen-pbr3`. Cross-compiled binaries are statically linked.

## Configuration

keen-pbr3 reads a JSON config file (default: `/etc/keen-pbr3/config.json`).

### Example config

```json
{
  "daemon": {
    "pid_file": "/var/run/keen-pbr3.pid",
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
      "file": "/etc/keen-pbr3/local.txt"
    }
  },
  "route": {
    "rules": [
      {
        "list": ["my-domains", "my-ips"],
        "outbound": "vpn"
      },
      {
        "list": ["blocked-sites"],
        "outbounds": ["vpn", "wan"]
      },
      {
        "list": ["local-list"],
        "action": "skip"
      }
    ],
    "fallback": "wan"
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

Each route rule matches traffic against one or more lists and takes an action:

- **Single outbound** (`"outbound": "tag"`) — route to the specified outbound
- **Failover chain** (`"outbounds": ["tag1", "tag2"]`) — try outbounds in order, use first healthy one
- **Skip** (`"action": "skip"`) — skip this rule, continue to next

Rules are evaluated in order; first match wins. Unmatched traffic uses the `fallback` outbound.

## Usage

```bash
keen-pbr3 [options]
```

### Options

| Flag | Description |
|---|---|
| `--config <path>` | Path to JSON config file (default: `/etc/keen-pbr3/config.json`) |
| `-d` | Daemonize (run in background) |
| `--no-api` | Disable REST API at runtime |
| `--version`, `-v` | Show version and exit |
| `--help`, `-h` | Show help and exit |

### Signals

| Signal | Effect |
|---|---|
| `SIGUSR1` | Trigger immediate list reload |
| `SIGTERM` / `SIGINT` | Graceful shutdown (cleanup routes, firewall, PID file) |

### REST API endpoints

Available when built with `with_api=true` and not disabled with `--no-api`:

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/status` | Daemon status, loaded lists, active outbounds |
| `GET` | `/api/health` | Health check results for all outbounds |
| `POST` | `/api/reload` | Trigger list re-download and re-apply |

### Running on the router

```bash
# Copy the binary to the router
scp dist/mips-le-keenetic/keen-pbr3 root@router:/opt/sbin/keen-pbr3

# Copy config
scp config.json root@router:/etc/keen-pbr3/config.json

# Run as daemon
/opt/sbin/keen-pbr3 --config /etc/keen-pbr3/config.json -d
```

### Dnsmasq integration

keen-pbr3 generates a dnsmasq config file at `/tmp/keen-pbr3-dnsmasq.conf` with `ipset=` and `server=` directives for domain-based routing. Add this to your dnsmasq configuration:

```
conf-file=/tmp/keen-pbr3-dnsmasq.conf
```

Then restart dnsmasq after keen-pbr3 starts.

## How it works

1. **Startup** — loads config, downloads/caches remote lists, reads local lists and inline entries
2. **Firewall setup** — creates ipsets/nft sets for IP/CIDR entries, adds mangle rules to mark matching packets with fwmarks
3. **Routing setup** — creates ip rules (fwmark → routing table) and routes (default via interface/gateway) in corresponding tables
4. **DNS setup** — generates dnsmasq config so resolved domain IPs get added to the appropriate ipsets at DNS resolution time
5. **Health monitoring** — periodically pings configured targets through their bound interfaces; circuit breaker prevents rapid failover
6. **Event loop** — epoll-based loop handles signals, timers, and optional API requests

## License

See [LICENSE](LICENSE) for details.
