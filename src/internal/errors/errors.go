// Package errors provides domain-specific error types for the keen-pbr application.
//
// This package defines structured errors with error codes, making it easier to handle
// and test different error conditions consistently across the application.
package errors

import "fmt"

// ErrorCode represents a category of error that can occur in the application.
type ErrorCode string

const (
	// ErrCodeConfig indicates a configuration-related error.
	ErrCodeConfig ErrorCode = "CONFIG_ERROR"

	// ErrCodeNetwork indicates a network configuration error (iptables, routes, rules).
	ErrCodeNetwork ErrorCode = "NETWORK_ERROR"

	// ErrCodeKeenetic indicates an error communicating with the Keenetic router.
	ErrCodeKeenetic ErrorCode = "KEENETIC_ERROR"

	// ErrCodeIPSet indicates an error related to ipset operations.
	ErrCodeIPSet ErrorCode = "IPSET_ERROR"

	// ErrCodeInterface indicates an error related to network interfaces.
	ErrCodeInterface ErrorCode = "INTERFACE_ERROR"

	// ErrCodeValidation indicates a validation error.
	ErrCodeValidation ErrorCode = "VALIDATION_ERROR"

	// ErrCodeList indicates an error related to list operations (download, parsing).
	ErrCodeList ErrorCode = "LIST_ERROR"

	// ErrCodeInternal indicates an unexpected internal error.
	ErrCodeInternal ErrorCode = "INTERNAL_ERROR"
)

// Error represents a domain-specific error with an error code and optional cause.
type Error struct {
	Code    ErrorCode
	Message string
	Cause   error
}

// Error implements the error interface.
func (e *Error) Error() string {
	if e.Cause != nil {
		return fmt.Sprintf("[%s] %s: %v", e.Code, e.Message, e.Cause)
	}
	return fmt.Sprintf("[%s] %s", e.Code, e.Message)
}

// Unwrap returns the underlying cause of the error for errors.Is and errors.As support.
func (e *Error) Unwrap() error {
	return e.Cause
}

// Is checks if the error matches the target error code.
func (e *Error) Is(target error) bool {
	if t, ok := target.(*Error); ok {
		return e.Code == t.Code
	}
	return false
}

// New creates a new domain error with the specified code and message.
func New(code ErrorCode, message string) *Error {
	return &Error{
		Code:    code,
		Message: message,
		Cause:   nil,
	}
}

// Wrap creates a new domain error wrapping an existing error.
func Wrap(code ErrorCode, message string, cause error) *Error {
	return &Error{
		Code:    code,
		Message: message,
		Cause:   cause,
	}
}

// NewConfigError creates a new configuration error.
func NewConfigError(message string, cause error) *Error {
	return Wrap(ErrCodeConfig, message, cause)
}

// NewNetworkError creates a new network configuration error.
func NewNetworkError(message string, cause error) *Error {
	return Wrap(ErrCodeNetwork, message, cause)
}

// NewKeeneticError creates a new Keenetic API error.
func NewKeeneticError(message string, cause error) *Error {
	return Wrap(ErrCodeKeenetic, message, cause)
}

// NewIPSetError creates a new ipset operation error.
func NewIPSetError(message string, cause error) *Error {
	return Wrap(ErrCodeIPSet, message, cause)
}

// NewInterfaceError creates a new interface-related error.
func NewInterfaceError(message string, cause error) *Error {
	return Wrap(ErrCodeInterface, message, cause)
}

// NewValidationError creates a new validation error.
func NewValidationError(message string, cause error) *Error {
	return Wrap(ErrCodeValidation, message, cause)
}

// NewListError creates a new list operation error.
func NewListError(message string, cause error) *Error {
	return Wrap(ErrCodeList, message, cause)
}

// NewInternalError creates a new internal error.
func NewInternalError(message string, cause error) *Error {
	return Wrap(ErrCodeInternal, message, cause)
}
