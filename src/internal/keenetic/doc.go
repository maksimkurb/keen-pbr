// Package keenetic provides a client for interacting with Keenetic Router RCI API.
//
// This package enables communication with Keenetic routers to retrieve interface
// information, version details, and DNS configuration. The client supports both
// legacy router versions and modern firmware, automatically detecting capabilities.
//
// # Features
//
//   - Interface detection and status monitoring
//   - DNS server configuration retrieval
//   - Version detection with automatic adapter selection
//   - Response caching for improved performance
//   - Concurrent-safe operations
//
// # Interface Detection
//
// The package provides two approaches for interface detection:
//
//   - Legacy: Uses /ip/hotspot/host endpoint (deprecated)
//   - Modern: Uses /system endpoint with system-name filtering
//
// The client automatically selects the appropriate method based on router capabilities.
//
// # Example Usage
//
// Creating a client and retrieving interfaces:
//
//	client := keenetic.NewClient(nil) // Uses default HTTP client
//	interfaces, err := client.GetInterfaces()
//	if err != nil {
//	    log.Fatal(err)
//	}
//
//	for name, iface := range interfaces {
//	    fmt.Printf("%s: %s (up=%v, connected=%v)\n",
//	        name, iface.Description, iface.Up, iface.Connected)
//	}
//
// Checking router version:
//
//	version, err := client.GetVersion()
//	if err != nil {
//	    log.Fatal(err)
//	}
//	fmt.Printf("Router: %s %s\n", version.Vendor, version.Model)
//
// The client implements the KeeneticClient interface from the domain package,
// enabling dependency injection and testing with mocks.
package keenetic
