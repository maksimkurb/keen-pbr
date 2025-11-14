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
	GetOutboundTags() []string // Returns all referenced outbound tags
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

func (s *StaticOutboundTable) GetOutboundTags() []string {
	return []string{s.Outbound}
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

func (u *URLTestOutboundTable) GetOutboundTags() []string {
	return u.Outbounds
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

// Rule represents a routing rule
type Rule struct {
	ID               string         `json:"id,omitempty"`
	Name             string         `json:"name,omitempty"`
	Enabled          bool           `json:"enabled"`
	Priority         int            `json:"priority"`
	CustomDNSServers []DNS          `json:"customDnsServers,omitempty"`
	Lists            []List         `json:"lists"`
	OutboundTable    OutboundTable  `json:"outboundTable"`
}

// Validate checks if the rule is valid
func (r *Rule) Validate() error {
	if r.OutboundTable == nil {
		return fmt.Errorf("outboundTable is required")
	}

	if err := r.OutboundTable.Validate(); err != nil {
		return fmt.Errorf("invalid outboundTable: %w", err)
	}

	for i, dns := range r.CustomDNSServers {
		if err := dns.Validate(); err != nil {
			return fmt.Errorf("invalid DNS server at index %d: %w", i, err)
		}
	}

	for i, list := range r.Lists {
		if err := list.Validate(); err != nil {
			return fmt.Errorf("invalid list at index %d: %w", i, err)
		}
	}

	return nil
}

// UnmarshalJSON implements custom unmarshaling for Rule
func (r *Rule) UnmarshalJSON(data []byte) error {
	type Alias Rule
	aux := &struct {
		Lists         []json.RawMessage `json:"lists"`
		OutboundTable json.RawMessage   `json:"outboundTable"`
		*Alias
	}{
		Alias: (*Alias)(r),
	}

	if err := json.Unmarshal(data, aux); err != nil {
		return err
	}

	// Unmarshal lists
	r.Lists = make([]List, len(aux.Lists))
	for i, rawList := range aux.Lists {
		list, err := UnmarshalList(rawList)
		if err != nil {
			return fmt.Errorf("error unmarshaling list at index %d: %w", i, err)
		}
		r.Lists[i] = list
	}

	// Unmarshal outbound table
	outboundTable, err := UnmarshalOutboundTable(aux.OutboundTable)
	if err != nil {
		return fmt.Errorf("error unmarshaling outboundTable: %w", err)
	}
	r.OutboundTable = outboundTable

	return nil
}

// MarshalJSON implements custom marshaling for Rule
func (r *Rule) MarshalJSON() ([]byte, error) {
	type Alias Rule

	// Marshal lists
	lists := make([]json.RawMessage, len(r.Lists))
	for i, list := range r.Lists {
		data, err := MarshalList(list)
		if err != nil {
			return nil, fmt.Errorf("error marshaling list at index %d: %w", i, err)
		}
		lists[i] = data
	}

	// Marshal outbound table
	outboundTable, err := MarshalOutboundTable(r.OutboundTable)
	if err != nil {
		return nil, fmt.Errorf("error marshaling outboundTable: %w", err)
	}

	return json.Marshal(&struct {
		Lists         []json.RawMessage `json:"lists"`
		OutboundTable json.RawMessage   `json:"outboundTable"`
		*Alias
	}{
		Lists:         lists,
		OutboundTable: outboundTable,
		Alias:         (*Alias)(r),
	})
}
