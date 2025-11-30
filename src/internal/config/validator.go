package config

import (
	"errors"
	"fmt"
	"net"
	"os"

	"github.com/go-playground/validator/v10"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

// ValidateConfig validates the entire configuration and returns all validation errors
func (c *Config) ValidateConfig() error {
	var validationErrors ValidationErrors

	// Validate general config
	if c.General == nil {
		validationErrors = append(validationErrors, ValidationError{
			FieldPath: "general",
			Message:   "configuration must contain 'general' section",
		})
		return validationErrors
	}

	// Use validator to validate General config
	if err := validate.Struct(c.General); err != nil {
		validationErrors = append(validationErrors, convertValidatorErrors(err, "general", "")...)
	}

	// Validate IPSets
	if len(c.IPSets) == 0 {
		validationErrors = append(validationErrors, ValidationError{
			FieldPath: "ipset",
			Message:   "configuration must contain at least one ipset",
		})
	} else {
		validationErrors = append(validationErrors, c.validateIPSets()...)
	}

	// Validate Lists
	validationErrors = append(validationErrors, c.validateLists()...)

	if len(validationErrors) > 0 {
		return validationErrors
	}

	return nil
}

func (c *Config) validateIPSets() ValidationErrors {
	var validationErrors ValidationErrors

	// Track duplicates
	seenNames := make(map[string]bool)
	seenTables := make(map[int]bool)
	seenPriorities := make(map[int]bool)
	seenFwmarks := make(map[uint32]bool)

	for i, ipset := range c.IPSets {
		itemName := ipset.IPSetName
		if itemName == "" {
			itemName = fmt.Sprintf("ipset[%d]", i)
		}

		// Validate struct fields
		if err := validate.Struct(ipset); err != nil {
			validationErrors = append(validationErrors, convertValidatorErrors(err, fmt.Sprintf("ipset.%d", i), itemName)...)
		}

		// Check duplicate ipset name
		if seenNames[ipset.IPSetName] {
			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: "ipset_name",
				Message:   fmt.Sprintf("duplicate ipset name: %s", ipset.IPSetName),
			})
		}
		seenNames[ipset.IPSetName] = true

		// Validate routing config exists
		if ipset.Routing == nil {
			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: "routing",
				Message:   "routing configuration is required",
			})
			continue
		}

		// Validate routing struct
		if err := validate.Struct(ipset.Routing); err != nil {
			validationErrors = append(validationErrors, convertValidatorErrors(err, fmt.Sprintf("ipset.%d.routing", i), itemName)...)
		}

		// Check duplicate routing table
		if seenTables[ipset.Routing.IPRouteTable] {
			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: "routing.table",
				Message:   fmt.Sprintf("duplicate routing table: %d", ipset.Routing.IPRouteTable),
			})
		}
		seenTables[ipset.Routing.IPRouteTable] = true

		// Check duplicate priority
		if seenPriorities[ipset.Routing.IPRulePriority] {
			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: "routing.priority",
				Message:   fmt.Sprintf("duplicate rule priority: %d", ipset.Routing.IPRulePriority),
			})
		}
		seenPriorities[ipset.Routing.IPRulePriority] = true

		// Check duplicate fwmark
		if seenFwmarks[ipset.Routing.FwMark] {
			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: "routing.fwmark",
				Message:   fmt.Sprintf("duplicate fwmark: %d", ipset.Routing.FwMark),
			})
		}
		seenFwmarks[ipset.Routing.FwMark] = true

		// Ensure either interfaces are configured OR default gateway is set
		hasInterfaces := len(ipset.Routing.Interfaces) > 0
		hasDefaultGateway := ipset.Routing.DefaultGateway != ""

		if !hasInterfaces && !hasDefaultGateway {
			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: "routing.interfaces",
				Message:   "must specify at least one interface or configure default_gateway",
			})
		}

		// Validate default gateway IP family matches ipset version
		if ipset.Routing.DefaultGateway != "" {
			ip := net.ParseIP(ipset.Routing.DefaultGateway)
			if ip != nil {
				if ipset.IPVersion == Ipv4 && ip.To4() == nil {
					validationErrors = append(validationErrors, ValidationError{
						ItemName:  itemName,
						FieldPath: "routing.default_gateway",
						Message:   fmt.Sprintf("IPv6 address %s cannot be used for IPv4 ipset", ipset.Routing.DefaultGateway),
					})
				} else if ipset.IPVersion == Ipv6 && ip.To4() != nil {
					validationErrors = append(validationErrors, ValidationError{
						ItemName:  itemName,
						FieldPath: "routing.default_gateway",
						Message:   fmt.Sprintf("IPv4 address %s cannot be used for IPv6 ipset", ipset.Routing.DefaultGateway),
					})
				}
			}
		}

		// Validate lists exist
		for _, listName := range ipset.Lists {
			found := false
			for _, list := range c.Lists {
				if list.ListName == listName {
					found = true
					break
				}
			}
			if !found {
				validationErrors = append(validationErrors, ValidationError{
					ItemName:  itemName,
					FieldPath: "lists",
					Message:   fmt.Sprintf("unknown list: %s", listName),
				})
			}
		}

		// Validate iptables rules
		for j, rule := range ipset.IPTablesRules {
			if rule.Chain == "" {
				validationErrors = append(validationErrors, ValidationError{
					ItemName:  itemName,
					FieldPath: fmt.Sprintf("iptables_rule.%d.chain", j),
					Message:   "chain cannot be empty",
				})
			}
			if rule.Table == "" {
				validationErrors = append(validationErrors, ValidationError{
					ItemName:  itemName,
					FieldPath: fmt.Sprintf("iptables_rule.%d.table", j),
					Message:   "table cannot be empty",
				})
			}
			if len(rule.Rule) == 0 {
				validationErrors = append(validationErrors, ValidationError{
					ItemName:  itemName,
					FieldPath: fmt.Sprintf("iptables_rule.%d.rule", j),
					Message:   "rule cannot be empty",
				})
			}
		}

		// Check duplicate interfaces
		seenIfaces := make(map[string]bool)
		for _, iface := range ipset.Routing.Interfaces {
			if seenIfaces[iface] {
				validationErrors = append(validationErrors, ValidationError{
					ItemName:  itemName,
					FieldPath: "routing.interfaces",
					Message:   fmt.Sprintf("duplicate interface: %s", iface),
				})
			}
			seenIfaces[iface] = true
		}
	}

	return validationErrors
}

func (c *Config) validateLists() ValidationErrors {
	var validationErrors ValidationErrors
	seenNames := make(map[string]bool)

	for i, list := range c.Lists {
		itemName := list.ListName
		if itemName == "" {
			itemName = fmt.Sprintf("list[%d]", i)
		}

		// Validate struct fields
		if err := validate.Struct(list); err != nil {
			validationErrors = append(validationErrors, convertValidatorErrors(err, fmt.Sprintf("list.%d", i), itemName)...)
		}

		// Check duplicate list name
		if seenNames[list.ListName] {
			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: "list_name",
				Message:   fmt.Sprintf("duplicate list name: %s", list.ListName),
			})
		}
		seenNames[list.ListName] = true

		// Validate that exactly one source is specified
		isURL := list.URL != ""
		isFile := list.File != ""
		isHosts := len(list.Hosts) > 0

		if !isURL && !isFile && !isHosts {
			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: "source",
				Message:   "must specify one of: url, file, or hosts",
			})
		}

		if (isURL && (isFile || isHosts)) || (isFile && isHosts) {
			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: "source",
				Message:   "can only specify one of: url, file, or hosts",
			})
		}

		// Validate file exists if specified
		if isFile {
			list.File = utils.GetAbsolutePath(list.File, c.GetConfigDir())
			if _, err := os.Stat(list.File); errors.Is(err, os.ErrNotExist) {
				validationErrors = append(validationErrors, ValidationError{
					ItemName:  itemName,
					FieldPath: "file",
					Message:   fmt.Sprintf("file does not exist: %s", list.File),
				})
			}
		}
	}

	return validationErrors
}

// convertValidatorErrors converts go-playground/validator errors to our ValidationError format
func convertValidatorErrors(err error, fieldPrefix string, itemName string) ValidationErrors {
	var validationErrors ValidationErrors

	var validatorErrs validator.ValidationErrors
	if errors.As(err, &validatorErrs) {
		for _, e := range validatorErrs {
			fieldPath := fieldPrefix
			if e.Field() != "" {
				// e.Field() now returns the TOML tag name because we registered TagNameFunc
				fieldName := e.Field()

				if fieldPrefix != "" {
					fieldPath = fieldPrefix + "." + fieldName
				} else {
					fieldPath = fieldName
				}
			}

			message := getValidationMessage(e)

			validationErrors = append(validationErrors, ValidationError{
				ItemName:  itemName,
				FieldPath: fieldPath,
				Message:   message,
			})
		}
	}

	return validationErrors
}
