// Package networking provides network configuration management for policy-based routing.
//
// This package implements the core networking functionality for keen-pbr, managing
// IPSets, IPTables rules, IP rules, and IP routes. It provides both persistent
// configuration (rules that stay active) and dynamic routing (adapts to interface changes).
//
// # Architecture
//
// The package is organized into specialized components:
//
//   - Manager: Main facade orchestrating all network operations
//   - PersistentConfigManager: Manages iptables rules and ip rules
//   - RoutingConfigManager: Manages dynamic ip routes
//   - InterfaceSelector: Chooses best available interface for routing
//   - IPSetManager: Manages ipset creation and population
//   - Builder patterns: Clean construction of IPTables and IPRule objects
//
// # Key Components
//
// IPSets: Named sets of IP addresses/networks for efficient packet matching
//
// IPTables: Firewall rules that mark packets matching ipsets with fwmark
//
// IP Rules: Policy routing rules that direct marked packets to custom tables
//
// IP Routes: Routes in custom tables pointing to specific interfaces
//
// # Example Usage
//
// Creating and using the network manager:
//
//	mgr := networking.NewManager(keeneticClient)
//
//	// Apply persistent configuration (iptables + ip rules)
//	if err := mgr.ApplyPersistentConfig(cfg); err != nil {
//	    log.Fatal(err)
//	}
//
//	// Apply dynamic routing (ip routes)
//	if err := mgr.ApplyRoutingConfig(cfg, nil); err != nil {
//	    log.Fatal(err)
//	}
//
//	// Update routing when interfaces change
//	if err := mgr.UpdateRouting(cfg, "wg0"); err != nil {
//	    log.Error(err)
//	}
//
// The package integrates with the Linux netlink API for ip rules/routes
// and uses iptables/ipset commands for firewall configuration.
package networking
