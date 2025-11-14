package models

import (
	"encoding/json"
	"fmt"
)

// OutboundTableType represents the type of outbound table
type OutboundTableType string

const (
	OutboundTableTypeStatic  OutboundTableType = "static"
	OutboundTableTypeURLTest OutboundTableType = "urltest"
)

// OutboundTable is an interface for different outbound table types
type OutboundTable interface {
	GetType() OutboundTableType
	Validate() error
}

// StaticOutboundTable represents a static outbound table with single outbound
type StaticOutboundTable struct {
	Type     OutboundTableType `json:"type"`
	Outbound string            `json:"outbound"` // outbound tag
}

func (s *StaticOutboundTable) GetType() OutboundTableType {
	return OutboundTableTypeStatic
}

func (s *StaticOutboundTable) Validate() error {
	if s.Outbound == "" {
		return fmt.Errorf("outbound tag is required for static table")
	}
	return nil
}

// URLTestOutboundTable represents an outbound table with URL-based selection
type URLTestOutboundTable struct {
	Type      OutboundTableType `json:"type"`
	Outbounds []string          `json:"outbounds"` // outbound tags
	TestURL   string            `json:"testUrl"`
}

func (u *URLTestOutboundTable) GetType() OutboundTableType {
	return OutboundTableTypeURLTest
}

func (u *URLTestOutboundTable) Validate() error {
	if len(u.Outbounds) == 0 {
		return fmt.Errorf("at least one outbound is required for urltest table")
	}
	if u.TestURL == "" {
		return fmt.Errorf("testUrl is required for urltest table")
	}
	return nil
}

// UnmarshalOutboundTable implements custom unmarshaling for OutboundTable interface
func UnmarshalOutboundTable(data []byte) (OutboundTable, error) {
	var typeCheck struct {
		Type OutboundTableType `json:"type"`
	}

	if err := json.Unmarshal(data, &typeCheck); err != nil {
		return nil, err
	}

	switch typeCheck.Type {
	case OutboundTableTypeStatic:
		var static StaticOutboundTable
		if err := json.Unmarshal(data, &static); err != nil {
			return nil, err
		}
		return &static, nil
	case OutboundTableTypeURLTest:
		var urlTest URLTestOutboundTable
		if err := json.Unmarshal(data, &urlTest); err != nil {
			return nil, err
		}
		return &urlTest, nil
	default:
		return nil, fmt.Errorf("unknown outbound table type: %s", typeCheck.Type)
	}
}

// MarshalOutboundTable marshals an OutboundTable interface to JSON
func MarshalOutboundTable(table OutboundTable) ([]byte, error) {
	return json.Marshal(table)
}
