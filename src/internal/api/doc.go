// Package api provides a REST API server for managing keen-pbr configuration.
//
// The API enables dynamic management of policy-based routing configuration through
// HTTP endpoints. It supports full CRUD operations for lists, ipsets, and settings,
// as well as service control and health monitoring.
//
// # Architecture
//
// The API is built using the chi router and follows RESTful principles:
//   - GET: Retrieve resources
//   - POST: Create new resources
//   - PUT: Update existing resources (full replacement)
//   - PATCH: Partial updates
//   - DELETE: Remove resources
//
// All endpoints are prefixed with /api/v1 for versioning.
//
// # Authentication
//
// Currently, no authentication is implemented. The API server should only be bound
// to localhost (127.0.0.1) for security.
//
// # Response Format
//
// Success responses use a standard wrapper:
//
//	{
//	  "data": { ... }
//	}
//
// Error responses use a structured format:
//
//	{
//	  "error": {
//	    "code": "error_code",
//	    "message": "Human-readable message",
//	    "details": { ... }
//	  }
//	}
//
// # Dependency Injection
//
// All handlers receive dependencies via the Handler struct, which contains:
//   - configPath: Path to the keen-pbr configuration file
//   - deps: AppDependencies container with all managed dependencies
//
// This enables proper testing with mock implementations and maintains consistency
// with the feature/3.0 branch architecture.
//
// # Configuration Management
//
// The API follows a read-modify-write pattern:
//  1. Load configuration from disk
//  2. Apply requested modifications
//  3. Validate complete configuration
//  4. Write back atomically
//  5. Restart services to apply changes
//  6. Return updated state
//
// # Usage
//
// Start the API server:
//
//	keen-pbr -config /opt/etc/keen-pbr/keen-pbr.conf server -bind 127.0.0.1:8080
//
// Access the API:
//
//	curl http://127.0.0.1:8080/api/v1/status
package api
