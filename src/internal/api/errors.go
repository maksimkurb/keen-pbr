package api

import (
	"encoding/json"
	"net/http"
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
)

// APIError represents a structured API error response.
type APIError struct {
	Code    ErrorCode              `json:"code"`
	Message string                 `json:"message"`
	Details map[string]interface{} `json:"details,omitempty"`
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
	json.NewEncoder(w).Encode(ErrorResponse{Error: err})
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
func WriteValidationError(w http.ResponseWriter, message string, details map[string]interface{}) {
	err := NewAPIError(ErrCodeValidationFailed, message).WithDetails(details)
	WriteError(w, http.StatusBadRequest, err)
}

// WriteServiceError writes a 500 Internal Server Error for service failures.
func WriteServiceError(w http.ResponseWriter, message string) {
	WriteError(w, http.StatusInternalServerError, NewAPIError(ErrCodeServiceError, message))
}
