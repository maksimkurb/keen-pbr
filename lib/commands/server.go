package commands

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/maksimkurb/keen-pbr/lib/api"
	"github.com/maksimkurb/keen-pbr/lib/log"
)

// CreateServerCommand creates a new server command
func CreateServerCommand() *ServerCommand {
	cmd := &ServerCommand{
		fs: flag.NewFlagSet("server", flag.ExitOnError),
	}

	cmd.fs.StringVar(&cmd.bindAddr, "bind", "127.0.0.1:8080", "Bind address for API server (e.g., 127.0.0.1:8080 or :8080)")

	return cmd
}

// ServerCommand implements the server command
type ServerCommand struct {
	fs       *flag.FlagSet
	ctx      *AppContext
	bindAddr string
}

// Name returns the command name
func (c *ServerCommand) Name() string {
	return c.fs.Name()
}

// Init initializes the server command
func (c *ServerCommand) Init(args []string, ctx *AppContext) error {
	c.ctx = ctx

	if err := c.fs.Parse(args); err != nil {
		return err
	}

	return nil
}

// Run executes the server command
func (c *ServerCommand) Run() error {
	log.Infof("Starting keen-pbr integrated server (API + Routing Service)")
	log.Infof("Config file: %s", c.ctx.ConfigPath)
	log.Infof("Bind address: %s", c.bindAddr)
	log.Infof("")
	log.Infof("API Documentation: .claude/REST.md")
	log.Infof("Example commands:")
	log.Infof("  curl http://%s/api/v1/status | jq", c.bindAddr)
	log.Infof("  curl http://%s/api/v1/lists | jq", c.bindAddr)
	log.Infof("")

	// Create API server with integrated routing service
	server := api.NewServer(c.ctx.ConfigPath, c.bindAddr, c.ctx.Interfaces)

	// Setup signal handling for graceful shutdown
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	// Start server in goroutine
	errChan := make(chan error, 1)
	go func() {
		if err := server.Start(); err != nil {
			errChan <- err
		}
	}()

	// Wait for interrupt or error
	select {
	case err := <-errChan:
		return fmt.Errorf("server error: %w", err)
	case sig := <-sigChan:
		log.Infof("Received signal: %v", sig)
		log.Infof("Shutting down gracefully...")

		// Graceful shutdown with timeout
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		if err := server.Stop(shutdownCtx); err != nil {
			return fmt.Errorf("error during shutdown: %w", err)
		}

		log.Infof("Server stopped")
		return nil
	}
}
