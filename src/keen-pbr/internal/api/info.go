package api

import (
	"encoding/json"
	"net"
	"net/http"
)

// NetworkInterface represents a network interface with its details
type NetworkInterface struct {
	Name string   `json:"name"`
	IPs  []string `json:"ips"`
	IsUp bool     `json:"isUp"`
}

// handleInterfaces returns information about all network interfaces
func (s *Server) handleInterfaces(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		respondError(w, http.StatusMethodNotAllowed, "Method not allowed")
		return
	}

	interfaces, err := net.Interfaces()
	if err != nil {
		respondError(w, http.StatusInternalServerError, "Failed to get network interfaces")
		return
	}

	result := make([]NetworkInterface, 0, len(interfaces))

	for _, iface := range interfaces {
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}

		ips := make([]string, 0, len(addrs))
		for _, addr := range addrs {
			if ipNet, ok := addr.(*net.IPNet); ok {
				ips = append(ips, ipNet.IP.String())
			}
		}

		result = append(result, NetworkInterface{
			Name: iface.Name,
			IPs:  ips,
			IsUp: iface.Flags&net.FlagUp != 0,
		})
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(result)
}
