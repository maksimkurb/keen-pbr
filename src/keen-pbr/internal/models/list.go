package models

import (
	"encoding/json"
	"fmt"
	"time"
)

// ListType represents the type of list
type ListType string

const (
	ListTypeLocal  ListType = "local"
	ListTypeRemote ListType = "remote"
)

// List is an interface for different list types
type List interface {
	GetType() ListType
	Validate() error
}

// LocalList represents a list stored locally
type LocalList struct {
	Type ListType `json:"type"`
	Path string   `json:"path"`
}

func (l *LocalList) GetType() ListType {
	return ListTypeLocal
}

func (l *LocalList) Validate() error {
	if l.Path == "" {
		return fmt.Errorf("path is required for local list")
	}
	return nil
}

// RemoteList represents a list fetched from a remote URL
type RemoteList struct {
	Type           ListType      `json:"type"`
	URL            string        `json:"url"`
	UpdateInterval time.Duration `json:"updateInterval"`
}

func (r *RemoteList) GetType() ListType {
	return ListTypeRemote
}

func (r *RemoteList) Validate() error {
	if r.URL == "" {
		return fmt.Errorf("url is required for remote list")
	}
	if r.UpdateInterval <= 0 {
		return fmt.Errorf("updateInterval must be positive")
	}
	return nil
}

// UnmarshalJSON implements custom unmarshaling for List interface
func UnmarshalList(data []byte) (List, error) {
	var typeCheck struct {
		Type ListType `json:"type"`
	}

	if err := json.Unmarshal(data, &typeCheck); err != nil {
		return nil, err
	}

	switch typeCheck.Type {
	case ListTypeLocal:
		var local LocalList
		if err := json.Unmarshal(data, &local); err != nil {
			return nil, err
		}
		return &local, nil
	case ListTypeRemote:
		var remote RemoteList
		if err := json.Unmarshal(data, &remote); err != nil {
			return nil, err
		}
		return &remote, nil
	default:
		return nil, fmt.Errorf("unknown list type: %s", typeCheck.Type)
	}
}

// MarshalList marshals a List interface to JSON
func MarshalList(list List) ([]byte, error) {
	return json.Marshal(list)
}
