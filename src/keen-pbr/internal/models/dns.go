package models

import (
	"encoding/json"
	"fmt"
)

// DNSType represents the type of DNS server
type DNSType string

const (
	DNSTypeUDP   DNSType = "udp"
	DNSTypeTLS   DNSType = "tls"
	DNSTypeHTTPS DNSType = "https"
)

// DNS represents a DNS server configuration
type DNS struct {
	Type            DNSType `json:"type"`
	Server          string  `json:"server"`
	Port            uint16  `json:"port"`
	Path            string  `json:"path,omitempty"`             // Used for HTTPS DNS
	ThroughOutbound bool    `json:"throughOutbound,omitempty"` // Default: true
}

// Validate checks if DNS configuration is valid
func (d *DNS) Validate() error {
	if d.Server == "" {
		return fmt.Errorf("server is required")
	}
	if d.Port == 0 {
		return fmt.Errorf("port is required")
	}
	switch d.Type {
	case DNSTypeUDP, DNSTypeTLS, DNSTypeHTTPS:
		// Valid types
	default:
		return fmt.Errorf("invalid DNS type: %s", d.Type)
	}
	if d.Type == DNSTypeHTTPS && d.Path == "" {
		return fmt.Errorf("path is required for HTTPS DNS")
	}
	return nil
}

// UnmarshalJSON implements custom unmarshaling with default values
func (d *DNS) UnmarshalJSON(data []byte) error {
	type Alias DNS
	aux := &struct {
		ThroughOutbound *bool `json:"throughOutbound,omitempty"`
		*Alias
	}{
		Alias: (*Alias)(d),
	}

	if err := json.Unmarshal(data, aux); err != nil {
		return err
	}

	// Set default value for ThroughOutbound
	if aux.ThroughOutbound != nil {
		d.ThroughOutbound = *aux.ThroughOutbound
	} else {
		d.ThroughOutbound = true
	}

	return nil
}
