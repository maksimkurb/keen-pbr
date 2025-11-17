// Package api provides the REST API server for keen-pbr configuration management.
//
// The API server allows dynamic management of keen-pbr configuration through HTTP
// REST endpoints. It provides:
//   - CRUD operations for lists and ipsets
//   - General settings management
//   - System status monitoring
//   - Service control (start/stop)
//   - Health checks and diagnostics
//
// # Architecture
//
// The API server is a separate process from the routing service:
//   - API Server (keen-pbr server): Manages configuration, controls service
//   - Routing Service (keen-pbr service): Performs actual routing operations
//
// # Response Format
//
// All successful responses wrap data in a "data" field:
//
//	{
//	  "data": { /* response payload */ }
//	}
//
// Error responses use the following format:
//
//	{
//	  "error": {
//	    "code": "ERROR_CODE",
//	    "message": "Human-readable error message",
//	    "details": { /* optional context */ }
//	  }
//	}
//
// # Service Restart
//
// Configuration changes automatically trigger service restart:
//  1. Stop keen-pbr service
//  2. Flush all ipsets
//  3. Start keen-pbr service
//  4. Restart dnsmasq
//
// See .claude/REST.md for complete API documentation.
package api
