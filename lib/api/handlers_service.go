package api

import (
	"encoding/json"
	"fmt"
	"net/http"
)

// ServiceControlRequest represents service control request
type ServiceControlRequest struct {
	Up bool `json:"up"`
}

// ServiceControlResponse represents service control response
type ServiceControlResponse struct {
	Status  string `json:"status"`
	Message string `json:"message"`
}

// HandleServiceControl starts or stops the integrated routing service
func HandleServiceControl(routingService *RoutingService) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req ServiceControlRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			RespondValidationError(w, fmt.Sprintf("Invalid request body: %v", err))
			return
		}

		var err error
		var action string

		if req.Up {
			action = "start"
			err = routingService.Start()
		} else {
			action = "stop"
			err = routingService.Stop()
		}

		if err != nil {
			RespondInternalError(w, fmt.Sprintf("Failed to %s routing service: %v", action, err))
			return
		}

		// Get current status
		status := routingService.GetStatus()
		message := fmt.Sprintf("Routing service %sed successfully", action)

		resp := ServiceControlResponse{
			Status:  status,
			Message: message,
		}

		RespondOK(w, resp)
	}
}
