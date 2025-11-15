package singbox

import (
	"fmt"
	"strconv"
	"strings"
)

// ParseWireGuard parses a WireGuard proxy URL
func ParseWireGuard(tag, rawURL string) (map[string]interface{}, error) {
	u, err := ParseURL(rawURL, 51820)
	if err != nil {
		return nil, fmt.Errorf("failed to parse WireGuard URL: %w", err)
	}

	// Check for WARP configuration (no private key)
	privateKey := u.GetParam("pk", "privatekey")
	if privateKey == "" {
		// This might be a WARP configuration
		// Note: WARP is not directly supported by vanilla sing-box
		return nil, fmt.Errorf("WARP configuration detected, not supported by sing-box 1.12.12")
	}

	outbound := map[string]interface{}{
		"type":        "wireguard",
		"tag":         tag,
		"server":      u.Hostname,
		"server_port": u.Port,
		"private_key": privateKey,
	}

	// Peer public key
	if peerPublicKey := u.GetParam("peer", "peerpublickey"); peerPublicKey != "" {
		outbound["peer_public_key"] = peerPublicKey
	}

	// Pre-shared key
	if preSharedKey := u.GetParam("psk", "presharedkey"); preSharedKey != "" {
		outbound["pre_shared_key"] = preSharedKey
	}

	// Reserved bytes
	if reserved := u.GetParam("reserved"); reserved != "" {
		parts := strings.Split(reserved, ",")
		reservedBytes := make([]int, 0, len(parts))
		for _, p := range parts {
			if b, err := strconv.Atoi(strings.TrimSpace(p)); err == nil {
				reservedBytes = append(reservedBytes, b)
			}
		}
		if len(reservedBytes) > 0 {
			outbound["reserved"] = reservedBytes
		}
	}

	// MTU
	if mtu := u.GetParamInt(1420, "mtu"); mtu != 1420 {
		outbound["mtu"] = mtu
	}

	// Local addresses
	if localAddr := u.GetParam("address", "localaddress"); localAddr != "" {
		addresses := strings.Split(localAddr, ",")
		localAddresses := make([]string, 0, len(addresses))
		for _, addr := range addresses {
			addr = strings.TrimSpace(addr)
			// Add /24 if no subnet is specified and it's an IPv4 address
			if !strings.Contains(addr, "/") && strings.Count(addr, ".") == 3 {
				addr = addr + "/24"
			}
			localAddresses = append(localAddresses, addr)
		}
		outbound["local_address"] = localAddresses
	}

	// Fake packets for obfuscation
	if fakePackets := u.GetParam("ifp", "wnoisecount"); fakePackets != "" {
		// Note: Fake packets might not be supported in sing-box 1.12.12
		// This is a custom extension in some forks
	}

	// Workers
	if workers := u.GetParamInt(0, "workers"); workers > 0 {
		outbound["workers"] = workers
	}

	return outbound, nil
}
