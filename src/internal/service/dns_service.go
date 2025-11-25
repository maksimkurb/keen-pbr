package service

import (
	"fmt"

	"github.com/maksimkurb/keen-pbr/src/internal/domain"
)

// DNSServerInfo represents a DNS server with formatted output support.
type DNSServerInfo struct {
	Type     string  `json:"type"`
	Endpoint string  `json:"endpoint"`
	Domain   *string `json:"domain,omitempty"`
	Proxy    string  `json:"proxy"`
	Port     string  `json:"port,omitempty"`
}

// DNSService provides unified DNS server retrieval for both CLI and API.
type DNSService struct {
	client domain.KeeneticClient
}

// NewDNSService creates a new DNS service.
func NewDNSService(client domain.KeeneticClient) *DNSService {
	return &DNSService{client: client}
}

// GetDNSServers retrieves DNS servers from the Keenetic router.
func (s *DNSService) GetDNSServers() ([]DNSServerInfo, error) {
	if s.client == nil {
		return nil, fmt.Errorf("keenetic client not available")
	}

	servers, err := s.client.GetDNSServers()
	if err != nil {
		return nil, fmt.Errorf("failed to fetch DNS servers: %w", err)
	}

	result := make([]DNSServerInfo, 0, len(servers))
	for _, server := range servers {
		result = append(result, DNSServerInfo{
			Type:     string(server.Type),
			Endpoint: server.Endpoint,
			Domain:   server.Domain,
			Proxy:    server.Proxy,
			Port:     server.Port,
		})
	}

	return result, nil
}
