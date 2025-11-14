package config

import (
	"encoding/json"
	"fmt"
	"os"
	"sync"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/models"
)

// Config represents the application configuration
type Config struct {
	Rules          map[string]*models.Rule          `json:"rules"`
	OutboundTables map[string]models.OutboundTable  `json:"outboundTables"`
	Outbounds      map[string]models.Outbound       `json:"outbounds"`

	mu sync.RWMutex
}

// New creates a new Config instance
func New() *Config {
	return &Config{
		Rules:          make(map[string]*models.Rule),
		OutboundTables: make(map[string]models.OutboundTable),
		Outbounds:      make(map[string]models.Outbound),
	}
}

// Load loads configuration from a file
func Load(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			// Return empty config if file doesn't exist
			return New(), nil
		}
		return nil, fmt.Errorf("failed to read config file: %w", err)
	}

	var rawConfig struct {
		Rules          map[string]json.RawMessage `json:"rules"`
		OutboundTables map[string]json.RawMessage `json:"outboundTables"`
		Outbounds      map[string]json.RawMessage `json:"outbounds"`
	}

	if err := json.Unmarshal(data, &rawConfig); err != nil {
		return nil, fmt.Errorf("failed to unmarshal config: %w", err)
	}

	cfg := New()

	// Unmarshal outbounds first (needed for rules)
	for tag, rawOutbound := range rawConfig.Outbounds {
		outbound, err := models.UnmarshalOutbound(rawOutbound)
		if err != nil {
			return nil, fmt.Errorf("failed to unmarshal outbound %s: %w", tag, err)
		}
		cfg.Outbounds[tag] = outbound
	}

	// Unmarshal outbound tables
	for id, rawTable := range rawConfig.OutboundTables {
		table, err := models.UnmarshalOutboundTable(rawTable)
		if err != nil {
			return nil, fmt.Errorf("failed to unmarshal outbound table %s: %w", id, err)
		}
		cfg.OutboundTables[id] = table
	}

	// Unmarshal rules
	for id, rawRule := range rawConfig.Rules {
		var rule models.Rule
		if err := json.Unmarshal(rawRule, &rule); err != nil {
			return nil, fmt.Errorf("failed to unmarshal rule %s: %w", id, err)
		}
		rule.ID = id
		cfg.Rules[id] = &rule
	}

	return cfg, nil
}

// Save saves configuration to a file
func (c *Config) Save(path string) error {
	c.mu.RLock()
	defer c.mu.RUnlock()

	data, err := c.marshalJSON()
	if err != nil {
		return fmt.Errorf("failed to marshal config: %w", err)
	}

	if err := os.WriteFile(path, data, 0644); err != nil {
		return fmt.Errorf("failed to write config file: %w", err)
	}

	return nil
}

// marshalJSON marshals config to JSON (caller must hold read lock)
func (c *Config) marshalJSON() ([]byte, error) {
	rawConfig := struct {
		Rules          map[string]json.RawMessage `json:"rules"`
		OutboundTables map[string]json.RawMessage `json:"outboundTables"`
		Outbounds      map[string]json.RawMessage `json:"outbounds"`
	}{
		Rules:          make(map[string]json.RawMessage),
		OutboundTables: make(map[string]json.RawMessage),
		Outbounds:      make(map[string]json.RawMessage),
	}

	// Marshal outbounds
	for tag, outbound := range c.Outbounds {
		data, err := models.MarshalOutbound(outbound)
		if err != nil {
			return nil, fmt.Errorf("failed to marshal outbound %s: %w", tag, err)
		}
		rawConfig.Outbounds[tag] = data
	}

	// Marshal outbound tables
	for id, table := range c.OutboundTables {
		data, err := models.MarshalOutboundTable(table)
		if err != nil {
			return nil, fmt.Errorf("failed to marshal outbound table %s: %w", id, err)
		}
		rawConfig.OutboundTables[id] = data
	}

	// Marshal rules
	for id, rule := range c.Rules {
		data, err := json.Marshal(rule)
		if err != nil {
			return nil, fmt.Errorf("failed to marshal rule %s: %w", id, err)
		}
		rawConfig.Rules[id] = data
	}

	return json.MarshalIndent(rawConfig, "", "  ")
}

// GetRule retrieves a rule by ID
func (c *Config) GetRule(id string) (*models.Rule, bool) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	rule, ok := c.Rules[id]
	return rule, ok
}

// GetAllRules retrieves all rules
func (c *Config) GetAllRules() map[string]*models.Rule {
	c.mu.RLock()
	defer c.mu.RUnlock()

	result := make(map[string]*models.Rule, len(c.Rules))
	for k, v := range c.Rules {
		result[k] = v
	}
	return result
}

// AddRule adds or updates a rule
func (c *Config) AddRule(id string, rule *models.Rule) error {
	if err := rule.Validate(); err != nil {
		return err
	}

	c.mu.Lock()
	defer c.mu.Unlock()

	rule.ID = id
	c.Rules[id] = rule
	return nil
}

// DeleteRule deletes a rule
func (c *Config) DeleteRule(id string) bool {
	c.mu.Lock()
	defer c.mu.Unlock()

	if _, exists := c.Rules[id]; !exists {
		return false
	}
	delete(c.Rules, id)
	return true
}

// GetOutboundTable retrieves an outbound table by ID
func (c *Config) GetOutboundTable(id string) (models.OutboundTable, bool) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	table, ok := c.OutboundTables[id]
	return table, ok
}

// GetAllOutboundTables retrieves all outbound tables
func (c *Config) GetAllOutboundTables() map[string]models.OutboundTable {
	c.mu.RLock()
	defer c.mu.RUnlock()

	result := make(map[string]models.OutboundTable, len(c.OutboundTables))
	for k, v := range c.OutboundTables {
		result[k] = v
	}
	return result
}

// AddOutboundTable adds or updates an outbound table
func (c *Config) AddOutboundTable(id string, table models.OutboundTable) error {
	if err := table.Validate(); err != nil {
		return err
	}

	c.mu.Lock()
	defer c.mu.Unlock()

	c.OutboundTables[id] = table
	return nil
}

// DeleteOutboundTable deletes an outbound table
func (c *Config) DeleteOutboundTable(id string) bool {
	c.mu.Lock()
	defer c.mu.Unlock()

	if _, exists := c.OutboundTables[id]; !exists {
		return false
	}
	delete(c.OutboundTables, id)
	return true
}

// ToJSON exports the entire config as JSON
func (c *Config) ToJSON() ([]byte, error) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.marshalJSON()
}
