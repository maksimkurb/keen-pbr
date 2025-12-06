package config

import (
	"fmt"
	"net"
	"reflect"
	"strings"

	"github.com/go-playground/validator/v10"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

// getValidationMessage returns a human-readable message for a validation error
func getValidationMessage(e validator.FieldError) string {
	switch e.Tag() {
	case "required":
		return "field is required"
	case "min":
		return fmt.Sprintf("must be >= %s", e.Param())
	case "max":
		return fmt.Sprintf("must be <= %s", e.Param())
	case "oneof":
		return fmt.Sprintf("must be one of: %s", e.Param())
	case "url":
		return "must be a valid URL"
	case "ip":
		return "must be a valid IP address"
	case "ipset_name":
		return "must consist only of lowercase letters, numbers, and underscores [a-z0-9_]"
	case "ip_or_empty":
		return "must be a valid IP address (IPv6 must be in square brackets, e.g., [::1]) or empty"
	case "hostport_or_empty":
		return "must be in format 'host:port' or empty"
	case "upstream_url":
		return "must be a valid upstream URL (keenetic://, udp://ip:port, or doh://host/path)"
	case "dns_override":
		return "must be a valid DNS override (ip or ip#port, IPv6 must be in square brackets)"
	default:
		return fmt.Sprintf("validation failed: %s", e.Tag())
	}
}

// ValidationError represents a single validation error with context
type ValidationError struct {
	ItemName  string // For lists/ipsets: the name of the item (e.g., "vpn1", "local-file")
	FieldPath string // Dot-notation field path (e.g., "general.fallback_dns", "routing.fwmark")
	Message   string // Human-readable error message
}

// ValidationErrors is a collection of validation errors
type ValidationErrors []ValidationError

// Error implements the error interface
func (ve ValidationErrors) Error() string {
	if len(ve) == 0 {
		return "no validation errors"
	}

	var sb strings.Builder
	sb.WriteString(fmt.Sprintf("validation failed with %d error(s):\n", len(ve)))
	for i, err := range ve {
		if err.ItemName != "" {
			sb.WriteString(fmt.Sprintf("  %d. [%s] %s: %s\n", i+1, err.ItemName, err.FieldPath, err.Message))
		} else {
			sb.WriteString(fmt.Sprintf("  %d. %s: %s\n", i+1, err.FieldPath, err.Message))
		}
	}
	return sb.String()
}

var validate *validator.Validate

func init() {
	validate = validator.New()

	// Register custom validators
	if err := validate.RegisterValidation("ip_or_empty", validateIPOrEmpty); err != nil {
		panic(err)
	}
	if err := validate.RegisterValidation("hostport_or_empty", validateHostPortOrEmpty); err != nil {
		panic(err)
	}
	if err := validate.RegisterValidation("upstream_url", validateUpstreamURLTag); err != nil {
		panic(err)
	}
	if err := validate.RegisterValidation("dns_override", validateDNSOverrideTag); err != nil {
		panic(err)
	}
	if err := validate.RegisterValidation("ipset_name", validateIPSetName); err != nil {
		panic(err)
	}

	// Register function to get field name from "toml" tag
	validate.RegisterTagNameFunc(func(fld reflect.StructField) string {
		name := strings.SplitN(fld.Tag.Get("toml"), ",", 2)[0]
		if name == "-" {
			return ""
		}
		return name
	})
}

// Custom validator: IP address or empty (IPv6 must be in square brackets)
func validateIPOrEmpty(fl validator.FieldLevel) bool {
	value := fl.Field().String()
	if value == "" {
		return true
	}
	return validateIPAddress(value)
}

// validateIPAddress validates IP address with IPv6 in square brackets
func validateIPAddress(value string) bool {
	// Check if it's in square brackets (IPv6 format)
	if strings.HasPrefix(value, "[") && strings.HasSuffix(value, "]") {
		addr := strings.Trim(value, "[]")
		// Allow [::] for dual-stack
		if addr == "::" {
			return true
		}
		// Must be valid IPv6
		ip := net.ParseIP(addr)
		return ip != nil && ip.To4() == nil
	}

	// Without brackets, must be IPv4
	ip := net.ParseIP(value)
	return ip != nil && ip.To4() != nil
}

// Custom validator: host:port format or empty
func validateHostPortOrEmpty(fl validator.FieldLevel) bool {
	value := fl.Field().String()
	if value == "" {
		return true
	}
	_, _, err := net.SplitHostPort(value)
	return err == nil
}

// Custom validator: upstream URL format
func validateUpstreamURLTag(fl validator.FieldLevel) bool {
	upstream := fl.Field().String()
	return validateUpstreamURL(upstream) == nil
}

// Custom validator: DNS override format (ip or ip#port, IPv6 must be in square brackets)
func validateDNSOverrideTag(fl validator.FieldLevel) bool {
	value := fl.Field().String()
	if value == "" {
		return true
	}

	// Check if it contains a port
	if portIndex := strings.LastIndex(value, "#"); portIndex != -1 {
		ip := value[:portIndex]
		port := value[portIndex+1:]
		return validateIPAddress(ip) && utils.IsValidPort(port)
	}
	return validateIPAddress(value)
}

// Custom validator: ipset name format
func validateIPSetName(fl validator.FieldLevel) bool {
	name := fl.Field().String()
	return ipsetRegexp.MatchString(name)
}

// validateUpstreamURL validates DNS upstream URL format
func validateUpstreamURL(upstream string) error {
	if upstream == "" {
		return fmt.Errorf("upstream URL cannot be empty")
	}

	if strings.HasPrefix(upstream, "keenetic://") {
		return nil
	}

	if strings.HasPrefix(upstream, "udp://") {
		addr := strings.TrimPrefix(upstream, "udp://")
		if _, _, err := net.SplitHostPort(addr); err != nil {
			return fmt.Errorf("invalid UDP upstream format (expected udp://ip:port)")
		}
		return nil
	}

	if strings.HasPrefix(upstream, "doh://") {
		url := strings.TrimPrefix(upstream, "doh://")
		if url == "" || !strings.Contains(url, "/") {
			return fmt.Errorf("invalid DoH upstream format (expected doh://host/path)")
		}
		return nil
	}

	return fmt.Errorf("unsupported upstream scheme (supported: keenetic://, udp://, doh://)")
}
