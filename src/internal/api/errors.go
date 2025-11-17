package api

import (
	"encoding/json"
	"net/http"
)

// ErrorCode represents API error codes
type ErrorCode string

const (
	ErrorCodeValidation ErrorCode = "VALIDATION_ERROR"
	ErrorCodeNotFound   ErrorCode = "NOT_FOUND"
	ErrorCodeConflict   ErrorCode = "CONFLICT"
	ErrorCodeInternal   ErrorCode = "INTERNAL_ERROR"
)

// APIError represents an API error response
type APIError struct {
	Code    ErrorCode              `json:"code"`
	Message string                 `json:"message"`
	Details map[string]interface{} `json:"details,omitempty"`
}

// ErrorResponse wraps an error in the standard error format
type ErrorResponse struct {
	Error APIError `json:"error"`
}

// NewError creates a new APIError
func NewError(code ErrorCode, message string) APIError {
	return APIError{
		Code:    code,
		Message: message,
		Details: nil,
	}
}

// NewErrorWithDetails creates a new APIError with details
func NewErrorWithDetails(code ErrorCode, message string, details map[string]interface{}) APIError {
	return APIError{
		Code:    code,
		Message: message,
		Details: details,
	}
}

// RespondError sends an error response with the appropriate HTTP status code
func RespondError(w http.ResponseWriter, statusCode int, err APIError) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	json.NewEncoder(w).Encode(ErrorResponse{Error: err})
}

// RespondValidationError sends a validation error response (400)
func RespondValidationError(w http.ResponseWriter, message string) {
	RespondError(w, http.StatusBadRequest, NewError(ErrorCodeValidation, message))
}

// RespondValidationErrorWithDetails sends a validation error with details (400)
func RespondValidationErrorWithDetails(w http.ResponseWriter, message string, details map[string]interface{}) {
	RespondError(w, http.StatusBadRequest, NewErrorWithDetails(ErrorCodeValidation, message, details))
}

// RespondNotFound sends a not found error response (404)
func RespondNotFound(w http.ResponseWriter, message string) {
	RespondError(w, http.StatusNotFound, NewError(ErrorCodeNotFound, message))
}

// RespondConflict sends a conflict error response (409)
func RespondConflict(w http.ResponseWriter, message string) {
	RespondError(w, http.StatusConflict, NewError(ErrorCodeConflict, message))
}

// RespondInternalError sends an internal error response (500)
func RespondInternalError(w http.ResponseWriter, message string) {
	RespondError(w, http.StatusInternalServerError, NewError(ErrorCodeInternal, message))
}
