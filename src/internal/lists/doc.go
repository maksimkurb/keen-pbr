// Package lists handles IP address list management for keen-pbr.
//
// This package provides functionality for downloading, parsing, and processing
// IP address lists from various sources including remote URLs, local files,
// and inline configuration. It supports automatic DNS resolution, hash-based
// change detection, and multiple list formats.
//
// # List Sources
//
// Lists can be sourced from:
//
//   - Remote URLs: Downloaded via HTTP/HTTPS with caching
//   - Local files: Read from filesystem paths
//   - Inline hosts: Defined directly in configuration
//
// # Features
//
//   - Automatic DNS resolution for hostnames
//   - MD5 hash-based change detection (skip unchanged lists)
//   - Support for comments and empty lines in list files
//   - IPv4 and IPv6 address parsing
//   - CIDR notation support with automatic /32 or /128 defaults
//   - Domain name filtering (non-IP entries are skipped)
//
// # Example Usage
//
// Downloading all lists from configuration:
//
//	if err := lists.DownloadLists(cfg); err != nil {
//	    log.Fatal(err)
//	}
//
// Extracting networks from a specific list:
//
//	listCfg := cfg.Lists[0]
//	networks, err := lists.GetNetworksFromList(&listCfg, cfg)
//	if err != nil {
//	    log.Fatal(err)
//	}
//
//	for _, network := range networks {
//	    fmt.Printf("%s\n", network.String())
//	}
//
// Processing lists with custom callbacks:
//
//	err := lists.IterateOverList(&listCfg, cfg, func(host string) error {
//	    // Process each line
//	    return nil
//	})
//
// The package automatically handles list validation and provides detailed
// error messages for troubleshooting.
package lists
