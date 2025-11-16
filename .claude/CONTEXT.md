# Keenetic Policy-Based Routing (keen-pbr) - Project Context

## Overview

**keen-pbr** is a policy-based routing toolkit for Keenetic routers written in Go. It enables selective traffic routing based on IP addresses, CIDR blocks, and domain names using ipset and dnsmasq integration.

**Language**: Go 1.23
**Module**: `github.com/maksimkurb/keen-pbr`
**License**: MIT

---

## Project Structure

```
keenetic-pbr-go/
├── src/                              # Go source code (standard layout)
│   ├── cmd/                          # Application entry points
│   │   └── keen-pbr/
│   │       └── main.go               # Main entry point - CLI flag parsing and command dispatch
│   │
│   └── internal/                     # Private application packages (not importable)
│       ├── commands/                 # CLI command implementations
│       │   ├── apply.go              # Apply routing rules (imports IPs to ipsets)
│       │   ├── dns.go                # Show DNS proxy profile
│       │   ├── download.go           # Download remote IP/domain lists
│       │   ├── dnsmasq_config.go     # Generate dnsmasq configuration
│       │   ├── interfaces.go         # List network interfaces
│       │   ├── self_check.go         # Validation and self-check
│       │   ├── undo.go               # Revert routing changes
│       │   ├── upgrade_config.go     # Upgrade configuration format
│       │   └── common.go             # Shared command utilities
│       │
│       ├── config/                   # Configuration management
│       │   ├── config.go             # Load and parse TOML configuration
│       │   ├── types.go              # Configuration data structures
│       │   └── validator.go          # Configuration validation logic
│       │
│       ├── networking/               # Network operations (Linux kernel APIs)
│       │   ├── network.go            # Coordinator for network operations
│       │   ├── interfaces.go         # Query network interface information
│       │   ├── ipset.go              # Manage ipset (efficient IP/CIDR storage)
│       │   ├── iptables.go           # Manage iptables rules
│       │   ├── iproute.go            # Manage ip route commands
│       │   ├── iprule.go             # Manage ip rule commands
│       │   ├── config_checker.go     # Verify network state
│       │   └── shell.go              # Shell command execution
│       │
│       ├── lists/                    # List processing (download, parse, import)
│       │   ├── downloader.go         # Download remote IP/domain lists
│       │   ├── domain_store.go       # Domain storage and filtering
│       │   ├── ipset_importer.go     # Import IPs to ipset
│       │   ├── dnsmasq_generator.go  # Generate dnsmasq configs
│       │   ├── hash_comparator.go    # Compare list hashes (avoid re-downloads)
│       │   └── common.go             # Shared list utilities
│       │
│       ├── keenetic/                 # Keenetic router-specific integration
│       │   ├── rci.go                # RCI API (Keenetic control interface)
│       │   └── common.go             # Shared Keenetic utilities
│       │
│       ├── utils/                    # General utilities
│       │   ├── ips.go                # IP/CIDR parsing and validation
│       │   ├── files.go              # File operations
│       │   ├── paths.go              # Path utilities
│       │   ├── validator.go          # General validation functions
│       │   └── bitset.go             # Bitset operations
│       │
│       ├── hashing/                  # Hashing utilities
│       │   └── md5proxy.go           # MD5 hash comparison for cache
│       │
│       └── log/                      # Logging
│           └── logger.go             # Logging implementation
│
├── .github/
│   └── workflows/
│       ├── build.yml                 # CI: Build binaries on every push (all branches)
│       └── release.yml               # CI: Create releases (only main + VERSION change)
│
├── package/                          # Package building and installation
│   ├── entware/                      # Entware/OpenWrt package definition
│   │   └── keen-pbr/
│   │       └── Makefile              # OpenWrt package build rules
│   │
│   └── etc/                          # Configuration files to install
│       ├── init.d/
│       │   └── S80keen-pbr           # Init script for daemon
│       ├── cron.daily/
│       │   └── 50-keen-pbr-lists-update.sh  # Daily list update cron job
│       ├── ndm/
│       │   ├── netfilter.d/
│       │   │   └── 50-keen-pbr-fwmarks.sh   # Firewall marking rules
│       │   └── ifstatechanged.d/
│       │       └── 50-keen-pbr-routing.sh   # Routing rules on interface changes
│       ├── keen-pbr/
│       │   ├── keen-pbr.conf         # Main configuration template
│       │   ├── local.lst             # Local list template
│       │   └── defaults              # Default values
│       ├── dnsmasq.d/
│       │   └── 100-keen-pbr.conf     # dnsmasq include template
│       └── dnsmasq.conf.keen-pbr     # dnsmasq config template
│
├── go.mod                            # Go module definition
├── go.sum                            # Go dependency lock file
├── VERSION                           # Version file (manually managed)
├── Makefile                          # Build orchestration
├── packages.mk                       # Local IPK package building rules
├── repository.mk                     # Package repository generation
├── README.md                         # Documentation (Russian)
├── README.en.md                      # Documentation (English)
└── keen-pbr.example.conf             # Example configuration file
```

---

## How It Works

### 1. **Configuration**
- Users create a TOML configuration file at `/opt/etc/keen-pbr/keen-pbr.conf`
- Config defines:
  - **Routing profiles**: Which interface/gateway to route through
  - **IP/CIDR lists**: Static IPs or remote list URLs
  - **Domain lists**: Domains to route through specific gateways

### 2. **List Management** (`download` command)
- Downloads remote lists (IP, CIDR, domain lists)
- Parses and validates entries
- Compares MD5 hashes to avoid re-downloading unchanged lists
- Stores in `/opt/etc/keen-pbr/lists.d/`

### 3. **Routing Application** (`apply` command)
- Reads configuration
- Creates ipsets (efficient kernel-space IP storage)
- Imports IPs/CIDRs from lists into ipsets
- Configures iptables rules to mark packets
- Sets up ip rules to route marked packets through specific gateways
- Adds ip routes for the routing tables

### 4. **DNS-based Routing** (`print-dnsmasq-config` command)
- Generates dnsmasq configuration
- Creates `ipset=/domain.com/ipset-name` entries
- When DNS resolves a domain, its IP is automatically added to the ipset
- Traffic to that IP is then routed according to ipset rules

### 5. **Network Integration**
- Uses Linux kernel APIs:
  - **ipset**: Efficient IP/CIDR matching (thousands of entries with O(1) lookup)
  - **iptables**: Packet marking with fwmark
  - **ip rule**: Policy-based routing rules
  - **ip route**: Custom routing tables
- Integrates with Keenetic's RCI API for router-specific features

---

## Build System

### Local Development
```bash
# Build binary
go build ./src/cmd/keen-pbr

# Run tests
go test ./src/internal/...

# Cross-compile for router
GOOS=linux GOARCH=mipsle go build ./src/cmd/keen-pbr
```

### Package Building

#### Local IPK Build (`make`)
- **Makefile**: Orchestrates build
- **packages.mk**: Builds IPK packages for architectures
  - Uses `go build -o ... ./src/cmd/keen-pbr`
  - Cross-compiles: mipsle, mips, arm64
  - Outputs: `out/<arch>/keen-pbr_<version>_<arch>.ipk`
- **repository.mk**: Creates HTML package index

```bash
make packages     # Build all architectures
make mipsel       # Build for mipsel only
make repository   # Generate package repository
```

#### Entware Build (CI)
- **package/entware/keen-pbr/Makefile**: OpenWrt build system
  - `GO_TARGET=./src/cmd/keen-pbr`
  - Integrates with Entware toolchain
  - Used by GitHub Actions

---

## CI/CD Workflows

### Build Workflow (`.github/workflows/build.yml`)
**Triggers**: Every push to any branch

**Actions**:
1. Builds IPK packages for all architectures (aarch64, mips, mipsel, x64, armv7)
2. Uploads artifacts to GitHub Actions

**Package Naming**:
- **Main branch**: `keen-pbr-2.2.2-entware-aarch64-3.10.ipk`
- **Other branches**: `keen-pbr-2.2.2-sha1a2b3c4-entware-aarch64-3.10.ipk`

**Purpose**: Development builds, CI validation, feature branch testing

---

### Release Workflow (`.github/workflows/release.yml`)
**Triggers**: Push to `main` branch with `VERSION` file change

**Actions**:
1. Builds IPK packages for all architectures
2. Creates GitHub Release (draft)
3. Tags version (e.g., `v2.2.3`)
4. Deploys package repository to GitHub Pages
5. Uploads IPK files to release

**Purpose**: Production releases

---

## CLI Commands

```bash
keen-pbr [options] <command>

Commands:
  download                # Download remote lists to lists.d directory
  apply                   # Import IPs/CIDRs from lists to ipsets
  print-dnsmasq-config    # Generate dnsmasq 'ipset=' entries
  interfaces              # Get available interfaces list
  self-check              # Run self-check and validation
  undo-routing            # Revert routing configuration
  dns                     # Show system DNS proxy profile

Options:
  -config string          # Path to config (default: /opt/etc/keen-pbr/keen-pbr.conf)
  -verbose                # Enable debug logging
```

---

## Key Dependencies

```
github.com/coreos/go-iptables v0.8.0          # iptables management
github.com/pelletier/go-toml/v2 v2.2.3        # TOML config parsing
github.com/valyala/fasttemplate v1.2.2        # Fast templating
github.com/vishvananda/netlink v1.3.0         # Netlink (ip route/rule)
golang.org/x/sys v0.10.0                      # System calls
```

---

## Deployment Flow

1. **Development**: Developer writes code, pushes to feature branch
2. **CI Build**: Build workflow creates artifacts with SHA in name
3. **Testing**: Developer downloads artifacts and tests on router
4. **Release Prep**: Developer updates VERSION file, commits to main
5. **Release**: Release workflow triggers, creates draft release
6. **Publishing**: Maintainer reviews draft release, publishes
7. **Distribution**: Users install from GitHub Pages package repository

---

## Router Installation

### Entware Package Manager
```bash
# Add repository (if not already added)
opkg update
opkg install keen-pbr

# Configuration
vi /opt/etc/keen-pbr/keen-pbr.conf

# Download lists
keen-pbr download

# Apply routing
keen-pbr apply

# Generate dnsmasq config
keen-pbr print-dnsmasq-config > /opt/etc/dnsmasq.d/keen-pbr.conf
/opt/etc/init.d/S56dnsmasq restart
```

### Manual Installation
1. Download IPK from GitHub Releases
2. Install: `opkg install keen-pbr_*.ipk`
3. Configure: Edit `/opt/etc/keen-pbr/keen-pbr.conf`
4. Enable: `/opt/etc/init.d/S80keen-pbr start`

---

## Architecture

### Standard Go Project Layout
- **`src/cmd/`**: Main applications (entry points)
- **`src/internal/`**: Private packages (cannot be imported by external projects)
- **No `pkg/`**: Not a library, so no public packages

### Why `internal/`?
- Prevents external projects from importing internal packages
- Signals that APIs may change without notice
- Appropriate for daemon/CLI applications

### Import Paths
All internal imports use:
```go
import "github.com/maksimkurb/keen-pbr/src/internal/commands"
import "github.com/maksimkurb/keen-pbr/src/internal/networking"
// etc.
```

---

## Common Tasks

### Adding a New Command
1. Create file in `src/internal/commands/<name>.go`
2. Implement command logic
3. Register in `src/cmd/keen-pbr/main.go`
4. Add tests in `src/internal/commands/<name>_test.go`

### Adding Network Functionality
1. Add to `src/internal/networking/<feature>.go`
2. Use existing shell execution or netlink APIs
3. Add tests with mocks in `src/internal/networking/<feature>_test.go`

### Updating Build Configuration
- **Local builds**: Edit `packages.mk`
- **CI builds**: Edit `.github/workflows/build.yml` or `release.yml`
- **Entware**: Edit `package/entware/keen-pbr/Makefile`

---

## Testing

```bash
# Run all tests
go test ./src/internal/...

# Run specific package tests
go test ./src/internal/networking

# Run with coverage
go test -cover ./src/internal/...

# Verbose output
go test -v ./src/internal/commands
```

---

## Notes

- **Version management**: VERSION file is manually updated by maintainer
- **No auto-versioning**: CI does not bump version automatically
- **Cross-compilation**: Uses GOOS/GOARCH for different router architectures
- **Target platforms**: Primarily Keenetic routers (MIPS/ARM) running Entware
- **Router integration**: Hooks into Keenetic's NDM system via scripts in `/opt/etc/ndm/`

---

## Project History

The project was recently restructured (November 2024) to follow standard Go project layout:
- Moved `main.go` → `src/cmd/keen-pbr/main.go`
- Moved `lib/` → `src/internal/`
- Updated import paths throughout codebase
- Split CI/CD into separate build and release workflows
