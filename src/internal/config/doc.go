// Package config handles configuration file parsing and validation for keen-pbr.
//
// This package reads TOML configuration files and provides strongly-typed
// structures for accessing configuration data. It supports automatic migration
// of deprecated configuration formats and provides comprehensive validation.
//
// # Configuration Structure
//
// The configuration file defines:
//   - General settings (lists output directory, Keenetic router URL)
//   - IP lists with their sources (URLs, files, or inline hosts)
//   - IPSets with routing configurations (interfaces, tables, rules)
//   - IPTables rules for packet marking and filtering
//
// # Supported Features
//
//   - TOML format with automatic type conversion
//   - Backward compatibility with deprecated field names
//   - IPv4 and IPv6 support with dual-stack configurations
//   - Template variables for iptables rules
//   - List source validation (URL, file, or inline hosts)
//   - Routing table and priority validation
//
// # Example Usage
//
// Loading and validating a configuration file:
//
//	cfg, err := config.LoadConfig("/etc/keen-pbr.conf")
//	if err != nil {
//	    log.Fatal(err)
//	}
//
// Accessing configuration:
//
//	for _, ipset := range cfg.IPSets {
//	    fmt.Printf("IPSet: %s, Version: IPv%d\n",
//	        ipset.IPSetName, ipset.IPVersion)
//	    for _, iface := range ipset.Routing.Interfaces {
//	        fmt.Printf("  Interface: %s\n", iface)
//	    }
//	}
//
// The package automatically handles deprecated configuration fields and
// provides clear error messages for validation failures.
package config
