// Package log provides simple leveled logging for keen-pbr.
//
// This package implements a lightweight logging system with colored output
// and support for different log levels: DEBUG, INFO, WARN, and ERROR.
// It provides global logging functions that can be used throughout the application.
//
// # Log Levels
//
//   - DEBUG: Detailed diagnostic information (only shown in verbose mode)
//   - INFO: General informational messages
//   - WARN: Warning messages for potentially problematic situations
//   - ERROR: Error messages for failures and exceptions
//
// # Features
//
//   - Colored console output with ANSI escape codes
//   - Configurable verbosity (debug logging on/off)
//   - Flexible output (stdout vs stderr)
//   - Printf-style formatting
//   - Fatal logging with immediate exit
//
// # Example Usage
//
// Basic logging:
//
//	log.Infof("Starting application")
//	log.Warnf("Configuration file not found at %s", path)
//	log.Errorf("Failed to connect: %v", err)
//
// Enabling verbose mode for debug output:
//
//	log.SetVerbose(true)
//	log.Debugf("Detailed trace: %+v", data)
//
// Fatal errors that exit the application:
//
//	if err != nil {
//	    log.Fatalf("Critical error: %v", err) // Exits with code 1
//	}
//
// Output control:
//
//	log.SetForceStdErr(true) // Send all logs to stderr
//
// The package uses global state for simplicity but provides thread-safe
// operations for concurrent use across goroutines.
package log
