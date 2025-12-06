package api

import (
	"net/http"
)

// ControlService controls the keen-pbr service.
// POST /api/v1/service
func (h *Handler) ControlService(w http.ResponseWriter, r *http.Request) {
	var req ServiceControlRequest
	if err := decodeJSON(r, &req); err != nil {
		WriteInvalidRequest(w, "Invalid JSON: "+err.Error())
		return
	}

	// Validate state
	if req.State != "started" && req.State != "stopped" && req.State != "restarted" {
		WriteInvalidRequest(w, "Invalid state. Must be one of: started, stopped, restarted")
		return
	}

	var err error
	var action string

	switch req.State {
	case "started":
		action = "start"
		err = h.serviceMgr.Start()
	case "stopped":
		action = "stop"
		err = h.serviceMgr.Stop()
	case "restarted":
		action = "restart"
		err = h.serviceMgr.Restart()
	}

	if err != nil {
		WriteServiceError(w, "Failed to "+action+" service: "+err.Error())
		return
	}

	response := ServiceControlResponse{
		Status:  "success",
		Message: "Service " + action + " command executed successfully",
	}

	writeJSONData(w, response)
}
