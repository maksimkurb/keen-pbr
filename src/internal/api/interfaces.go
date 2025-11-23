package api

import (
	"net/http"

	"github.com/maksimkurb/keen-pbr/src/internal/service"
)

// InterfaceInfo represents a network interface for API responses.
// Uses the shared InterfaceInfo type from service package.
type InterfaceInfo = service.InterfaceInfo

// InterfacesResponse represents the response for the interfaces list endpoint.
type InterfacesResponse struct {
	Interfaces []InterfaceInfo `json:"interfaces"`
}

// GetInterfaces returns a list of all network interfaces on the system.
// This endpoint is not cached to ensure fresh data when combobox is opened.
// Now includes Keenetic metadata when available.
func (h *Handler) GetInterfaces(w http.ResponseWriter, r *http.Request) {
	// Use shared InterfaceService
	ifaceService := service.NewInterfaceService(h.deps.KeeneticClient())
	interfaces, err := ifaceService.GetInterfaces(false, false) // No IPs, no loopback
	if err != nil {
		WriteInternalError(w, "Failed to get network interfaces: "+err.Error())
		return
	}

	response := InterfacesResponse{
		Interfaces: interfaces,
	}

	writeJSONData(w, response)
}

// DNSServersResponse represents the response for the DNS servers endpoint.
// Uses DNSServerInfo from types.go (which uses the shared service type).
type DNSServersResponse struct {
	Servers []DNSServerInfo `json:"servers"`
}

// GetDNSServers returns a list of DNS servers configured on the Keenetic router.
func (h *Handler) GetDNSServers(w http.ResponseWriter, r *http.Request) {
	// Use shared DNSService
	dnsService := service.NewDNSService(h.deps.KeeneticClient())
	servers, err := dnsService.GetDNSServers()
	if err != nil {
		WriteInternalError(w, "Failed to get DNS servers: "+err.Error())
		return
	}

	response := DNSServersResponse{
		Servers: servers,
	}

	writeJSONData(w, response)
}
