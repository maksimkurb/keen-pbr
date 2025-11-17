// Package utils provides general-purpose utility functions for keen-pbr.
//
// This package contains various helper functions and data structures that
// are used across the application, including IP address manipulation,
// path handling, file operations, validation, and bit manipulation.
//
// # Components
//
//   - IP utilities: Convert between IP/netmask and CIDR notation
//   - Path utilities: Handle absolute and relative paths
//   - File utilities: Safe file closing and operations
//   - Validation: DNS name and domain validation (from govalidator)
//   - BitSet: Efficient bit manipulation data structure
//
// # Example Usage
//
// IP address conversion:
//
//	ipNet, err := utils.IPv4ToNetmask("192.168.1.0", "255.255.255.0")
//	if err != nil {
//	    log.Fatal(err)
//	}
//	fmt.Printf("Network: %s\n", ipNet.String()) // 192.168.1.0/24
//
// IPv6 conversion:
//
//	ipNet, err := utils.IPv6ToNetmask("2001:db8::", 64)
//	if err != nil {
//	    log.Fatal(err)
//	}
//
// Path resolution:
//
//	absPath := utils.GetAbsolutePath("lists/mylist.txt", "/etc/keen-pbr")
//	// Returns: /etc/keen-pbr/lists/mylist.txt
//
// DNS validation:
//
//	if utils.IsDNSName("example.com") {
//	    fmt.Println("Valid DNS name")
//	}
//
// BitSet operations:
//
//	bs := utils.NewBitSet(100)
//	bs.Add(5)
//	bs.Add(42)
//	if bs.Has(5) {
//	    fmt.Printf("BitSet has %d bits set\n", bs.Count())
//	}
//
// The utilities in this package are designed to be simple, focused, and
// reusable across different parts of the application.
package utils
