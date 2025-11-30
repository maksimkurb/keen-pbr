package api

import (
	"encoding/json"
	"errors"
	"net/http"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// ErrorCode represents standard API error codes.
type ErrorCode string

const (
	// ErrCodeInvalidRequest indicates malformed or invalid request data.
	ErrCodeInvalidRequest ErrorCode = "invalid_request"

	// ErrCodeNotFound indicates the requested resource was not found.
	ErrCodeNotFound ErrorCode = "not_found"

	// ErrCodeConflict indicates a resource conflict (e.g., duplicate name).
	ErrCodeConflict ErrorCode = "conflict"

	// ErrCodeInternalError indicates an internal server error.
	ErrCodeInternalError ErrorCode = "internal_error"

	// ErrCodeValidationFailed indicates configuration validation failed.
	ErrCodeValidationFailed ErrorCode = "validation_failed"

	// ErrCodeServiceError indicates a service operation failed.
	ErrCodeServiceError ErrorCode = "service_error"

	// ErrCodeForbidden indicates access is forbidden.
	ErrCodeForbidden ErrorCode = "forbidden"
)

// APIError represents a structured API error response.
type APIError struct {
	Code        ErrorCode               `json:"code"`
	Message     string                  `json:"message"`
	Details     map[string]interface{}  `json:"details,omitempty"`
	FieldErrors []ValidationErrorDetail `json:"fieldErrors,omitempty"`
}

// ErrorResponse wraps an APIError for JSON responses.
type ErrorResponse struct {
	Error APIError `json:"error"`
}

// NewAPIError creates a new APIError with the given code and message.
func NewAPIError(code ErrorCode, message string) APIError {
	return APIError{
		Code:    code,
		Message: message,
		Details: nil,
	}
}

// WithDetails adds details to an APIError.
func (e APIError) WithDetails(details map[string]interface{}) APIError {
	e.Details = details
	return e
}

// WriteError writes an error response to the HTTP response writer.
func WriteError(w http.ResponseWriter, statusCode int, err APIError) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	if err := json.NewEncoder(w).Encode(ErrorResponse{Error: err}); err != nil {
		log.Warnf("Failed to encode error response: %v", err)
	}
}

// WriteInvalidRequest writes a 400 Bad Request error.
func WriteInvalidRequest(w http.ResponseWriter, message string) {
	WriteError(w, http.StatusBadRequest, NewAPIError(ErrCodeInvalidRequest, message))
}

// WriteNotFound writes a 404 Not Found error.
func WriteNotFound(w http.ResponseWriter, resource string) {
	WriteError(w, http.StatusNotFound, NewAPIError(ErrCodeNotFound, resource+" not found"))
}

// WriteConflict writes a 409 Conflict error.
func WriteConflict(w http.ResponseWriter, message string) {
	WriteError(w, http.StatusConflict, NewAPIError(ErrCodeConflict, message))
}

// WriteInternalError writes a 500 Internal Server Error.
func WriteInternalError(w http.ResponseWriter, message string) {
	WriteError(w, http.StatusInternalServerError, NewAPIError(ErrCodeInternalError, message))
}

// WriteValidationError writes a 400 Bad Request with validation details.
// If err is a config.ValidationErrors, it will be formatted as an array of errors.
// Otherwise, it falls back to the legacy format with a message string.
func WriteValidationError(w http.ResponseWriter, err error) {
	var validationErrs config.ValidationErrors

	// Check if the error is ValidationErrors
	if errors.As(err, &validationErrs) {
		// Convert to API format
		fieldErrors := make([]ValidationErrorDetail, len(validationErrs))
		for i, ve := range validationErrs {
			fieldErrors[i] = ValidationErrorDetail{
				Field:   ve.FieldPath,
				Message: ve.Message,
			}
		}

		// Create error response with field errors array
		apiErr := APIError{
			Code:        ErrCodeValidationFailed,
			Message:     "Configuration validation failed",
			FieldErrors: fieldErrors,
		}
		WriteError(w, http.StatusBadRequest, apiErr)
	} else {
		// Legacy format for non-ValidationErrors
		apiErr := NewAPIError(ErrCodeValidationFailed, err.Error())
		WriteError(w, http.StatusBadRequest, apiErr)
	}
}

// WriteServiceError writes a 500 Internal Server Error for service failures.
func WriteServiceError(w http.ResponseWriter, message string) {
	WriteError(w, http.StatusInternalServerError, NewAPIError(ErrCodeServiceError, message))
}

// WriteForbidden writes a 403 Forbidden error.
func WriteForbidden(w http.ResponseWriter, message string) {
	WriteError(w, http.StatusForbidden, NewAPIError(ErrCodeForbidden, message))
}
