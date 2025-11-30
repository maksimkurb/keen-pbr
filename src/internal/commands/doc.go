// Package commands implements CLI command handlers for keen-pbr.
//
// This package provides the command-line interface layer for the application,
// implementing subcommands like apply, download, undo-routing, service, and self-check.
// Each command implements the Runner interface and delegates business logic to
// the service layer.
//
// # Command Structure
//
// All commands follow a consistent pattern:
//   - Init(): Parse arguments and validate configuration
//   - Run(): Execute command using service layer
//   - Name(): Return command name for routing
//
// # Available Commands
//
//   - apply: Apply routing configuration and populate ipsets
//   - download: Download IP lists from remote sources
//   - undo-routing: Remove all routing configuration
//   - service: Run as a daemon with automatic interface monitoring
//   - self-check: Verify current system configuration
//
// # Example Usage
//
// Creating and running a command:
//
//	cmd := commands.CreateApplyCommand()
//	ctx := &commands.AppContext{
//	    ConfigPath: "/etc/keen-pbr.conf",
//	    Verbose:    true,
//	}
//	if err := cmd.Init(args, ctx); err != nil {
//	    log.Fatal(err)
//	}
//	if err := cmd.Run(); err != nil {
//	    log.Fatal(err)
//	}
//
// Commands are thin wrappers that orchestrate service layer operations,
// keeping CLI concerns separate from business logic.
package commands
