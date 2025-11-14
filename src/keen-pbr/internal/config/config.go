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
	Rules     map[string]*models.Rule    `json:"rules"`
	Outbounds map[string]models.Outbound `json:"outbounds"`

	mu sync.RWMutex
}

// New creates a new Config instance
func New() *Config {
	return &Config{
		Rules:     make(map[string]*models.Rule),
		Outbounds: make(map[string]models.Outbound),
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
		Rules     map[string]json.RawMessage `json:"rules"`
		Outbounds map[string]json.RawMessage `json:"outbounds"`
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
		Rules     map[string]json.RawMessage `json:"rules"`
		Outbounds map[string]json.RawMessage `json:"outbounds"`
	}{
		Rules:     make(map[string]json.RawMessage),
		Outbounds: make(map[string]json.RawMessage),
	}

	// Marshal outbounds
	for tag, outbound := range c.Outbounds {
		data, err := models.MarshalOutbound(outbound)
		if err != nil {
			return nil, fmt.Errorf("failed to marshal outbound %s: %w", tag, err)
		}
		rawConfig.Outbounds[tag] = data
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

// GetOutbound retrieves an outbound by tag
func (c *Config) GetOutbound(tag string) (models.Outbound, bool) {
	c.mu.RLock()
	defer c.mu.RUnlock()

	outbound, exists := c.Outbounds[tag]
	return outbound, exists
}

// GetAllOutbounds returns all outbounds
func (c *Config) GetAllOutbounds() map[string]models.Outbound {
	c.mu.RLock()
	defer c.mu.RUnlock()

	outbounds := make(map[string]models.Outbound, len(c.Outbounds))
	for tag, outbound := range c.Outbounds {
		outbounds[tag] = outbound
	}
	return outbounds
}

// AddOutbound adds a new outbound
func (c *Config) AddOutbound(tag string, outbound models.Outbound) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if _, exists := c.Outbounds[tag]; exists {
		return fmt.Errorf("outbound with tag %s already exists", tag)
	}
	c.Outbounds[tag] = outbound
	return nil
}

// UpdateOutbound updates an existing outbound
func (c *Config) UpdateOutbound(tag string, outbound models.Outbound) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if _, exists := c.Outbounds[tag]; !exists {
		return fmt.Errorf("outbound with tag %s not found", tag)
	}
	c.Outbounds[tag] = outbound
	return nil
}

// DeleteOutbound deletes an outbound
func (c *Config) DeleteOutbound(tag string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if _, exists := c.Outbounds[tag]; !exists {
		return fmt.Errorf("outbound with tag %s not found", tag)
	}
	delete(c.Outbounds, tag)
	return nil
}

// ReplaceAllRules replaces all rules atomically
func (c *Config) ReplaceAllRules(rules []*models.Rule, availableOutbounds map[string]bool) error {
	// Validate all rules first
	for i, rule := range rules {
		if err := rule.Validate(); err != nil {
			return fmt.Errorf("invalid rule at index %d: %w", i, err)
		}

		// Validate that all referenced outbounds exist
		for _, tag := range rule.OutboundTable.GetOutboundTags() {
			if !availableOutbounds[tag] {
				return fmt.Errorf("rule %s references non-existent outbound: %s", rule.ID, tag)
			}
		}
	}

	c.mu.Lock()
	defer c.mu.Unlock()

	// Clear existing rules
	c.Rules = make(map[string]*models.Rule, len(rules))

	// Add all new rules
	for _, rule := range rules {
		if rule.ID == "" {
			return fmt.Errorf("rule ID cannot be empty")
		}
		c.Rules[rule.ID] = rule
	}

	return nil
}

// ReplaceAllOutbounds replaces all outbounds atomically
func (c *Config) ReplaceAllOutbounds(outbounds []models.Outbound) error {
	// Validate all outbounds first
	tagMap := make(map[string]bool)
	for i, outbound := range outbounds {
		if err := outbound.Validate(); err != nil {
			return fmt.Errorf("invalid outbound at index %d: %w", i, err)
		}
		tag := outbound.GetTag()
		if tag == "" {
			return fmt.Errorf("outbound at index %d has empty tag", i)
		}
		if tagMap[tag] {
			return fmt.Errorf("duplicate outbound tag: %s", tag)
		}
		tagMap[tag] = true
	}

	c.mu.Lock()
	defer c.mu.Unlock()

	// Clear existing outbounds
	c.Outbounds = make(map[string]models.Outbound, len(outbounds))

	// Add all new outbounds
	for _, outbound := range outbounds {
		c.Outbounds[outbound.GetTag()] = outbound
	}

	return nil
}

// ToJSON exports the entire config as JSON
func (c *Config) ToJSON() ([]byte, error) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.marshalJSON()
}
