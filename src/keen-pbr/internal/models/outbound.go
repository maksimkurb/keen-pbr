package models

import (
	"encoding/json"
	"fmt"
)

// OutboundType represents the type of outbound connection
type OutboundType string

const (
	OutboundTypeInterface OutboundType = "interface"
	OutboundTypeProxy     OutboundType = "proxy"
)

// Outbound is an interface for different outbound types
type Outbound interface {
	GetTag() string
	GetType() OutboundType
	Validate() error
}

// InterfaceOutbound represents an outbound through a network interface
type InterfaceOutbound struct {
	Tag     string       `json:"tag"`
	Type    OutboundType `json:"type"`
	IfName  string       `json:"ifname"`
}

func (i *InterfaceOutbound) GetTag() string {
	return i.Tag
}

func (i *InterfaceOutbound) GetType() OutboundType {
	return OutboundTypeInterface
}

func (i *InterfaceOutbound) Validate() error {
	if i.Tag == "" {
		return fmt.Errorf("tag is required")
	}
	if i.IfName == "" {
		return fmt.Errorf("ifname is required for interface outbound")
	}
	return nil
}

// ProxyOutbound represents an outbound through a proxy
type ProxyOutbound struct {
	Tag  string       `json:"tag"`
	Type OutboundType `json:"type"`
	URL  string       `json:"url"`
}

func (p *ProxyOutbound) GetTag() string {
	return p.Tag
}

func (p *ProxyOutbound) GetType() OutboundType {
	return OutboundTypeProxy
}

func (p *ProxyOutbound) Validate() error {
	if p.Tag == "" {
		return fmt.Errorf("tag is required")
	}
	if p.URL == "" {
		return fmt.Errorf("url is required for proxy outbound")
	}
	return nil
}

// UnmarshalOutbound implements custom unmarshaling for Outbound interface
func UnmarshalOutbound(data []byte) (Outbound, error) {
	var typeCheck struct {
		Type OutboundType `json:"type"`
	}

	if err := json.Unmarshal(data, &typeCheck); err != nil {
		return nil, err
	}

	switch typeCheck.Type {
	case OutboundTypeInterface:
		var iface InterfaceOutbound
		if err := json.Unmarshal(data, &iface); err != nil {
			return nil, err
		}
		return &iface, nil
	case OutboundTypeProxy:
		var proxy ProxyOutbound
		if err := json.Unmarshal(data, &proxy); err != nil {
			return nil, err
		}
		return &proxy, nil
	default:
		return nil, fmt.Errorf("unknown outbound type: %s", typeCheck.Type)
	}
}

// MarshalOutbound marshals an Outbound interface to JSON
func MarshalOutbound(outbound Outbound) ([]byte, error) {
	return json.Marshal(outbound)
}
