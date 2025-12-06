package keenetic

import (
	"fmt"
	"strconv"
	"strings"
)

// parseVersion parses version string like "4.03.C.6.3-9" and returns major/minor.
//
// The version string format is expected to have at least two dot-separated parts,
// where the first two parts are the major and minor version numbers.
//
// Example: "4.03.C.6.3-9" returns KeeneticVersion{Major: 4, Minor: 3}
func parseVersion(versionStr string) (*KeeneticVersion, error) {
	parts := strings.Split(versionStr, ".")
	if len(parts) < 2 {
		return nil, fmt.Errorf("invalid version format: %s", versionStr)
	}

	major, err := strconv.Atoi(parts[0])
	if err != nil {
		return nil, fmt.Errorf("invalid major version: %s", parts[0])
	}

	minor, err := strconv.Atoi(parts[1])
	if err != nil {
		return nil, fmt.Errorf("invalid minor version: %s", parts[1])
	}

	return &KeeneticVersion{Major: major, Minor: minor}, nil
}

// supportsSystemNameEndpoint returns true if Keenetic OS version supports
// the /show/interface/system-name endpoint (available in version 4.03+).
//
// This endpoint provides a direct way to query the Linux system interface name
// for a Keenetic interface ID, eliminating the need for IP address matching.
func supportsSystemNameEndpoint(version *KeeneticVersion) bool {
	if version == nil {
		return false
	}
	return version.Major > 4 || (version.Major == 4 && version.Minor >= 3)
}
