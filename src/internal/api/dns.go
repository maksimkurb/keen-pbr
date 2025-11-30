package api

import (
	"net/http"

	"github.com/maksimkurb/keen-pbr/src/internal/service"
)

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
