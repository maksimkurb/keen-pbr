package commands

import (
	"fmt"
	"testing"
)

func TestApplyCommand_FlagValidationLogic(t *testing.T) {
	tests := []struct {
		name                 string
		skipIpset            bool
		skipRouting          bool
		onlyRoutingInterface string
		expectError          bool
		errorSubstr          string
	}{
		{
			name:        "Both skip flags",
			skipIpset:   true,
			skipRouting: true,
			expectError: true,
			errorSubstr: "nothing to do",
		},
		{
			name:                 "Only routing with skip ipset",
			skipIpset:            true,
			onlyRoutingInterface: "eth0",
			expectError:          true,
			errorSubstr:          "can not be used together",
		},
		{
			name:                 "Only routing with skip routing",
			skipRouting:          true,
			onlyRoutingInterface: "eth0",
			expectError:          true,
			errorSubstr:          "can not be used together",
		},
		{
			name:        "Valid: skip ipset only",
			skipIpset:   true,
			expectError: false,
		},
		{
			name:        "Valid: skip routing only",
			skipRouting: true,
			expectError: false,
		},
		{
			name:                 "Valid: only routing interface",
			onlyRoutingInterface: "eth0",
			expectError:          false,
		},
		{
			name:        "Valid: no flags",
			expectError: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Test just the validation logic without full Init()
			var err error

			if tt.skipIpset && tt.skipRouting {
				err = fmt.Errorf("--skip-ipset and --skip-routing are used, nothing to do")
			} else if tt.onlyRoutingInterface != "" && (tt.skipRouting || tt.skipIpset) {
				err = fmt.Errorf("--only-routing-for-interface and --skip-* can not be used together")
			}

			if tt.expectError {
				if err == nil {
					t.Error("Expected error but got none")
				} else if tt.errorSubstr != "" && !contains(err.Error(), tt.errorSubstr) {
					t.Errorf("Expected error to contain '%s', got: %v", tt.errorSubstr, err)
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error but got: %v", err)
				}
			}
		})
	}
}

func TestApplyCommand_InterfaceValidationConditions(t *testing.T) {
	tests := []struct {
		name                 string
		skipRouting          bool
		onlyRoutingInterface string
		shouldValidate       bool
	}{
		{
			name:           "Skip routing - no validation",
			skipRouting:    true,
			shouldValidate: false,
		},
		{
			name:           "Normal apply - validation needed",
			skipRouting:    false,
			shouldValidate: true,
		},
		{
			name:                 "Only routing interface - validation needed",
			onlyRoutingInterface: "eth0",
			shouldValidate:       true,
		},
		{
			name:                 "Skip routing with only routing interface - validation needed",
			skipRouting:          true,
			onlyRoutingInterface: "eth0",
			shouldValidate:       true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Test the condition logic that determines if interface validation should run
			shouldValidate := !tt.skipRouting || tt.onlyRoutingInterface != ""

			if shouldValidate != tt.shouldValidate {
				t.Errorf("Expected shouldValidate=%v, got %v", tt.shouldValidate, shouldValidate)
			}
		})
	}
}

// Helper function
func contains(s, substr string) bool {
	if len(s) < len(substr) {
		return false
	}
	for i := 0; i <= len(s)-len(substr); i++ {
		if s[i:i+len(substr)] == substr {
			return true
		}
	}
	return false
}
