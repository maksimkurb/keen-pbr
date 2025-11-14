package models

import (
	"encoding/json"
	"fmt"
)

// Rule represents a routing rule
type Rule struct {
	ID               string     `json:"id,omitempty"`
	CustomDNSServers []DNS      `json:"customDnsServers,omitempty"`
	Lists            []List     `json:"lists"`
	Outbound         Outbound   `json:"outbound"`
}

// Validate checks if the rule is valid
func (r *Rule) Validate() error {
	if r.Outbound == nil {
		return fmt.Errorf("outbound is required")
	}

	if err := r.Outbound.Validate(); err != nil {
		return fmt.Errorf("invalid outbound: %w", err)
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
		Lists    []json.RawMessage `json:"lists"`
		Outbound json.RawMessage   `json:"outbound"`
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

	// Unmarshal outbound
	outbound, err := UnmarshalOutbound(aux.Outbound)
	if err != nil {
		return fmt.Errorf("error unmarshaling outbound: %w", err)
	}
	r.Outbound = outbound

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

	// Marshal outbound
	outbound, err := MarshalOutbound(r.Outbound)
	if err != nil {
		return nil, fmt.Errorf("error marshaling outbound: %w", err)
	}

	return json.Marshal(&struct {
		Lists    []json.RawMessage `json:"lists"`
		Outbound json.RawMessage   `json:"outbound"`
		*Alias
	}{
		Lists:    lists,
		Outbound: outbound,
		Alias:    (*Alias)(r),
	})
}
