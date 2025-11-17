package api

import (
	"encoding/json"
	"fmt"
	"net/http"
	"regexp"

	"github.com/go-chi/chi/v5"
	"github.com/maksimkurb/keen-pbr/lib/config"
)

var ipsetNameRegex = regexp.MustCompile(`^[a-z][a-z0-9_]*$`)

// IPSetResponse represents an ipset in API responses
type IPSetResponse struct {
	IPSetName           string              `json:"ipset_name"`
	Lists               []string            `json:"lists"`
	IPVersion           int                 `json:"ip_version"`
	FlushBeforeApplying bool                `json:"flush_before_applying"`
	Routing             *RoutingResponse    `json:"routing"`
	IPTablesRules       []*IPTablesRuleResp `json:"iptables_rules"`
}

// RoutingResponse represents routing config in API responses
type RoutingResponse struct {
	Interfaces  []string `json:"interfaces"`
	KillSwitch  bool     `json:"kill_switch"`
	FwMark      uint32   `json:"fwmark"`
	Table       int      `json:"table"`
	Priority    int      `json:"priority"`
	OverrideDNS string   `json:"override_dns"`
}

// IPTablesRuleResp represents an iptables rule in API responses
type IPTablesRuleResp struct {
	Chain string   `json:"chain"`
	Table string   `json:"table"`
	Rule  []string `json:"rule"`
}

// IPSetRequest represents an ipset in API requests
type IPSetRequest struct {
	IPSetName           string            `json:"ipset_name,omitempty"`
	Lists               []string          `json:"lists"`
	IPVersion           int               `json:"ip_version"`
	FlushBeforeApplying bool              `json:"flush_before_applying"`
	Routing             *RoutingRequest   `json:"routing"`
}

// RoutingRequest represents routing config in API requests
type RoutingRequest struct {
	Interfaces  []string `json:"interfaces"`
	KillSwitch  bool     `json:"kill_switch"`
	FwMark      uint32   `json:"fwmark"`
	Table       int      `json:"table"`
	Priority    int      `json:"priority"`
	OverrideDNS string   `json:"override_dns,omitempty"`
}

// toIPSetResponse converts config.IPSetConfig to IPSetResponse
func toIPSetResponse(ipset *config.IPSetConfig) IPSetResponse {
	routing := &RoutingResponse{
		Interfaces:  ipset.Routing.Interfaces,
		KillSwitch:  ipset.Routing.KillSwitch,
		FwMark:      ipset.Routing.FwMark,
		Table:       ipset.Routing.IpRouteTable,
		Priority:    ipset.Routing.IpRulePriority,
		OverrideDNS: ipset.Routing.DNSOverride,
	}

	rules := make([]*IPTablesRuleResp, 0, len(ipset.IPTablesRules))
	for _, rule := range ipset.IPTablesRules {
		rules = append(rules, &IPTablesRuleResp{
			Chain: rule.Chain,
			Table: rule.Table,
			Rule:  rule.Rule,
		})
	}

	return IPSetResponse{
		IPSetName:           ipset.IPSetName,
		Lists:               ipset.Lists,
		IPVersion:           int(ipset.IPVersion),
		FlushBeforeApplying: ipset.FlushBeforeApplying,
		Routing:             routing,
		IPTablesRules:       rules,
	}
}

// HandleIPSetsList returns all ipsets
func HandleIPSetsList(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		cfg, err := LoadConfig(configPath)
		if err != nil {
			RespondInternalError(w, fmt.Sprintf("Failed to load config: %v", err))
			return
		}

		ipsets := make([]IPSetResponse, 0, len(cfg.IPSets))
		for _, ipset := range cfg.IPSets {
			ipsets = append(ipsets, toIPSetResponse(ipset))
		}

		RespondOK(w, ipsets)
	}
}

// HandleIPSetsGet returns a single ipset
func HandleIPSetsGet(configPath string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		ipsetName := chi.URLParam(r, "ipset_name")

		cfg, err := LoadConfig(configPath)
		if err != nil {
			RespondInternalError(w, fmt.Sprintf("Failed to load config: %v", err))
			return
		}

		// Find ipset
		for _, ipset := range cfg.IPSets {
			if ipset.IPSetName == ipsetName {
				RespondOK(w, toIPSetResponse(ipset))
				return
			}
		}

		RespondNotFound(w, fmt.Sprintf("IPSet '%s' not found", ipsetName))
	}
}

// HandleIPSetsCreate creates a new ipset
func HandleIPSetsCreate(configPath string, routingService *RoutingService) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req IPSetRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			RespondValidationError(w, fmt.Sprintf("Invalid request body: %v", err))
			return
		}

		// Validation
		if req.IPSetName == "" {
			RespondValidationError(w, "ipset_name is required")
			return
		}

		if !ipsetNameRegex.MatchString(req.IPSetName) {
			RespondValidationErrorWithDetails(w,
				"ipset_name must match pattern ^[a-z][a-z0-9_]*$",
				map[string]interface{}{"field": "ipset_name", "value": req.IPSetName})
			return
		}

		if req.IPVersion != 4 && req.IPVersion != 6 {
			RespondValidationError(w, "ip_version must be 4 or 6")
			return
		}

		if len(req.Lists) == 0 {
			RespondValidationError(w, "lists must be non-empty")
			return
		}

		if req.Routing == nil {
			RespondValidationError(w, "routing configuration is required")
			return
		}

		if len(req.Routing.Interfaces) == 0 {
			RespondValidationError(w, "routing.interfaces must be non-empty")
			return
		}

		if req.Routing.FwMark == 0 || req.Routing.Table == 0 || req.Routing.Priority == 0 {
			RespondValidationError(w, "fwmark, table, and priority must be positive integers")
			return
		}

		// Modify config
		err := ModifyConfig(configPath, routingService, func(cfg *config.Config) error {
			// Check if ipset already exists
			for _, ipset := range cfg.IPSets {
				if ipset.IPSetName == req.IPSetName {
					return fmt.Errorf("ipset '%s' already exists", req.IPSetName)
				}
			}

			// Verify all referenced lists exist
			for _, listName := range req.Lists {
				found := false
				for _, list := range cfg.Lists {
					if list.ListName == listName {
						found = true
						break
					}
				}
				if !found {
					return fmt.Errorf("referenced list '%s' does not exist", listName)
				}
			}

			// Create new ipset
			newIPSet := &config.IPSetConfig{
				IPSetName:           req.IPSetName,
				Lists:               req.Lists,
				IPVersion:           config.IpFamily(req.IPVersion),
				FlushBeforeApplying: req.FlushBeforeApplying,
				Routing: &config.RoutingConfig{
					Interfaces:     req.Routing.Interfaces,
					KillSwitch:     req.Routing.KillSwitch,
					FwMark:         req.Routing.FwMark,
					IpRouteTable:   req.Routing.Table,
					IpRulePriority: req.Routing.Priority,
					DNSOverride:    req.Routing.OverrideDNS,
				},
				IPTablesRules: []*config.IPTablesRule{},
			}

			cfg.IPSets = append(cfg.IPSets, newIPSet)
			return nil
		})

		if err != nil {
			errMsg := err.Error()
			if errMsg == fmt.Sprintf("ipset '%s' already exists", req.IPSetName) {
				RespondConflict(w, errMsg)
			} else {
				RespondInternalError(w, errMsg)
			}
			return
		}

		// Load updated config to get the created ipset
		cfg, _ := LoadConfig(configPath)
		for _, ipset := range cfg.IPSets {
			if ipset.IPSetName == req.IPSetName {
				RespondCreated(w, toIPSetResponse(ipset))
				return
			}
		}

		RespondInternalError(w, "Failed to retrieve created ipset")
	}
}

// HandleIPSetsUpdate updates an existing ipset
func HandleIPSetsUpdate(configPath string, routingService *RoutingService) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		ipsetName := chi.URLParam(r, "ipset_name")

		var req IPSetRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			RespondValidationError(w, fmt.Sprintf("Invalid request body: %v", err))
			return
		}

		// Validation
		if req.IPVersion != 4 && req.IPVersion != 6 {
			RespondValidationError(w, "ip_version must be 4 or 6")
			return
		}

		if len(req.Lists) == 0 {
			RespondValidationError(w, "lists must be non-empty")
			return
		}

		if req.Routing == nil {
			RespondValidationError(w, "routing configuration is required")
			return
		}

		if len(req.Routing.Interfaces) == 0 {
			RespondValidationError(w, "routing.interfaces must be non-empty")
			return
		}

		if req.Routing.FwMark == 0 || req.Routing.Table == 0 || req.Routing.Priority == 0 {
			RespondValidationError(w, "fwmark, table, and priority must be positive integers")
			return
		}

		// Modify config
		err := ModifyConfig(configPath, routingService, func(cfg *config.Config) error {
			// Find ipset
			var targetIPSet *config.IPSetConfig
			for _, ipset := range cfg.IPSets {
				if ipset.IPSetName == ipsetName {
					targetIPSet = ipset
					break
				}
			}

			if targetIPSet == nil {
				return fmt.Errorf("ipset '%s' not found", ipsetName)
			}

			// Verify all referenced lists exist
			for _, listName := range req.Lists {
				found := false
				for _, list := range cfg.Lists {
					if list.ListName == listName {
						found = true
						break
					}
				}
				if !found {
					return fmt.Errorf("referenced list '%s' does not exist", listName)
				}
			}

			// Update ipset
			targetIPSet.Lists = req.Lists
			targetIPSet.IPVersion = config.IpFamily(req.IPVersion)
			targetIPSet.FlushBeforeApplying = req.FlushBeforeApplying
			targetIPSet.Routing.Interfaces = req.Routing.Interfaces
			targetIPSet.Routing.KillSwitch = req.Routing.KillSwitch
			targetIPSet.Routing.FwMark = req.Routing.FwMark
			targetIPSet.Routing.IpRouteTable = req.Routing.Table
			targetIPSet.Routing.IpRulePriority = req.Routing.Priority
			targetIPSet.Routing.DNSOverride = req.Routing.OverrideDNS

			return nil
		})

		if err != nil {
			errMsg := err.Error()
			if errMsg == fmt.Sprintf("ipset '%s' not found", ipsetName) {
				RespondNotFound(w, errMsg)
			} else {
				RespondInternalError(w, errMsg)
			}
			return
		}

		// Load updated config to get the updated ipset
		cfg, _ := LoadConfig(configPath)
		for _, ipset := range cfg.IPSets {
			if ipset.IPSetName == ipsetName {
				RespondOK(w, toIPSetResponse(ipset))
				return
			}
		}

		RespondInternalError(w, "Failed to retrieve updated ipset")
	}
}

// HandleIPSetsDelete deletes an ipset
func HandleIPSetsDelete(configPath string, routingService *RoutingService) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		ipsetName := chi.URLParam(r, "ipset_name")

		// Modify config
		err := ModifyConfig(configPath, routingService, func(cfg *config.Config) error {
			// Find and remove ipset
			for i, ipset := range cfg.IPSets {
				if ipset.IPSetName == ipsetName {
					cfg.IPSets = append(cfg.IPSets[:i], cfg.IPSets[i+1:]...)
					return nil
				}
			}

			return fmt.Errorf("ipset '%s' not found", ipsetName)
		})

		if err != nil {
			if err.Error() == fmt.Sprintf("ipset '%s' not found", ipsetName) {
				RespondNotFound(w, err.Error())
			} else {
				RespondInternalError(w, err.Error())
			}
			return
		}

		RespondNoContent(w)
	}
}
