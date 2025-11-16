package errors

import (
	"errors"
	"testing"
)

func TestError_Error(t *testing.T) {
	tests := []struct {
		name     string
		err      *Error
		expected string
	}{
		{
			name:     "error without cause",
			err:      New(ErrCodeConfig, "invalid configuration"),
			expected: "[CONFIG_ERROR] invalid configuration",
		},
		{
			name:     "error with cause",
			err:      Wrap(ErrCodeNetwork, "failed to apply routes", errors.New("permission denied")),
			expected: "[NETWORK_ERROR] failed to apply routes: permission denied",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.err.Error(); got != tt.expected {
				t.Errorf("Error() = %v, want %v", got, tt.expected)
			}
		})
	}
}

func TestError_Unwrap(t *testing.T) {
	cause := errors.New("underlying error")
	err := Wrap(ErrCodeInternal, "wrapper", cause)

	if unwrapped := err.Unwrap(); unwrapped != cause {
		t.Errorf("Unwrap() = %v, want %v", unwrapped, cause)
	}
}

func TestError_Is(t *testing.T) {
	err1 := New(ErrCodeConfig, "test error")
	err2 := New(ErrCodeConfig, "another error")
	err3 := New(ErrCodeNetwork, "network error")

	if !err1.Is(err2) {
		t.Errorf("Expected errors with same code to match")
	}

	if err1.Is(err3) {
		t.Errorf("Expected errors with different codes to not match")
	}
}

func TestNewConfigError(t *testing.T) {
	cause := errors.New("file not found")
	err := NewConfigError("failed to load config", cause)

	if err.Code != ErrCodeConfig {
		t.Errorf("Expected code %v, got %v", ErrCodeConfig, err.Code)
	}

	if err.Message != "failed to load config" {
		t.Errorf("Expected message 'failed to load config', got %v", err.Message)
	}

	if err.Cause != cause {
		t.Errorf("Expected cause to be preserved")
	}
}

func TestNewNetworkError(t *testing.T) {
	err := NewNetworkError("iptables error", nil)

	if err.Code != ErrCodeNetwork {
		t.Errorf("Expected code %v, got %v", ErrCodeNetwork, err.Code)
	}
}

func TestNewKeeneticError(t *testing.T) {
	err := NewKeeneticError("API unreachable", nil)

	if err.Code != ErrCodeKeenetic {
		t.Errorf("Expected code %v, got %v", ErrCodeKeenetic, err.Code)
	}
}

func TestNewIPSetError(t *testing.T) {
	err := NewIPSetError("ipset not found", nil)

	if err.Code != ErrCodeIPSet {
		t.Errorf("Expected code %v, got %v", ErrCodeIPSet, err.Code)
	}
}

func TestNewInterfaceError(t *testing.T) {
	err := NewInterfaceError("interface down", nil)

	if err.Code != ErrCodeInterface {
		t.Errorf("Expected code %v, got %v", ErrCodeInterface, err.Code)
	}
}

func TestNewValidationError(t *testing.T) {
	err := NewValidationError("invalid IP address", nil)

	if err.Code != ErrCodeValidation {
		t.Errorf("Expected code %v, got %v", ErrCodeValidation, err.Code)
	}
}

func TestNewListError(t *testing.T) {
	err := NewListError("download failed", nil)

	if err.Code != ErrCodeList {
		t.Errorf("Expected code %v, got %v", ErrCodeList, err.Code)
	}
}

func TestNewInternalError(t *testing.T) {
	err := NewInternalError("unexpected error", nil)

	if err.Code != ErrCodeInternal {
		t.Errorf("Expected code %v, got %v", ErrCodeInternal, err.Code)
	}
}
