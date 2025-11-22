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
			err:      &Error{Code: ErrCodeConfig, Message: "invalid configuration"},
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
	err1 := &Error{Code: ErrCodeConfig, Message: "test error"}
	err2 := &Error{Code: ErrCodeConfig, Message: "another error"}
	err3 := &Error{Code: ErrCodeNetwork, Message: "network error"}

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

