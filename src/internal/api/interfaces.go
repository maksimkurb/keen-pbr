package api

import (
	"net"
	"net/http"
)

// InterfaceInfo represents a network interface.
type InterfaceInfo struct {
	Name string `json:"name"`
}

// InterfacesResponse represents the response for the interfaces list endpoint.
type InterfacesResponse struct {
	Interfaces []InterfaceInfo `json:"interfaces"`
}

// GetInterfaces returns a list of all network interfaces on the system.
// This endpoint is not cached to ensure fresh data when combobox is opened.
func (h *Handler) GetInterfaces(w http.ResponseWriter, r *http.Request) {
	interfaces, err := net.Interfaces()
	if err != nil {
		WriteInternalError(w, "Failed to get network interfaces: "+err.Error())
		return
	}

	interfaceInfos := make([]InterfaceInfo, 0, len(interfaces))
	for _, iface := range interfaces {
		// Skip loopback and down interfaces
		if iface.Flags&net.FlagLoopback != 0 || iface.Flags&net.FlagUp == 0 {
			continue
		}
		interfaceInfos = append(interfaceInfos, InterfaceInfo{
			Name: iface.Name,
		})
	}

	response := InterfacesResponse{
		Interfaces: interfaceInfos,
	}

	writeJSONData(w, response)
}
