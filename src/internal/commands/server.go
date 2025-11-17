package commands

import (
	"context"
	"flag"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/api"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// ServerCommand implements the server command for running the HTTP API server.
type ServerCommand struct {
	fs   *flag.FlagSet
	ctx  *AppContext
	cfg  *config.Config
	deps *domain.AppDependencies

	// Command-specific flags
	bindAddr string
}

// CreateServerCommand creates a new server command.
func CreateServerCommand() Runner {
	return &ServerCommand{}
}

// Name returns the command name.
func (c *ServerCommand) Name() string {
	return "server"
}

// Init initializes the server command with arguments.
func (c *ServerCommand) Init(args []string, ctx *AppContext) error {
	c.ctx = ctx
	c.fs = flag.NewFlagSet("server", flag.ExitOnError)

	// Define command-specific flags
	c.fs.StringVar(&c.bindAddr, "bind", "127.0.0.1:8080", "Address to bind the HTTP server (e.g., 127.0.0.1:8080)")

	// Parse flags
	if err := c.fs.Parse(args); err != nil {
		return err
	}

	// Load and validate configuration
	cfg, err := loadAndValidateConfigOrFail(ctx.ConfigPath)
	if err != nil {
		return err
	}
	c.cfg = cfg

	// Create dependencies
	c.deps = domain.NewDefaultDependencies()

	return nil
}

// Run starts the HTTP API server.
func (c *ServerCommand) Run() error {
	log.Infof("Starting keen-pbr API server on %s", c.bindAddr)
	log.Infof("Configuration loaded from: %s", c.ctx.ConfigPath)

	// Create router
	router := api.NewRouter(c.ctx.ConfigPath, c.deps)

	// Create HTTP server
	server := &http.Server{
		Addr:         c.bindAddr,
		Handler:      router,
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	// Channel to listen for errors from the server
	serverErrors := make(chan error, 1)

	// Start server in a goroutine
	go func() {
		log.Infof("API server listening on http://%s", c.bindAddr)
		log.Infof("API endpoints available at http://%s/api/v1", c.bindAddr)
		serverErrors <- server.ListenAndServe()
	}()

	// Channel to listen for interrupt signals
	shutdown := make(chan os.Signal, 1)
	signal.Notify(shutdown, os.Interrupt, syscall.SIGTERM, syscall.SIGINT)

	// Block until we receive a signal or an error
	select {
	case err := <-serverErrors:
		if err != nil && err != http.ErrServerClosed {
			return fmt.Errorf("server error: %w", err)
		}

	case sig := <-shutdown:
		log.Infof("Received signal %v, shutting down server...", sig)

		// Create context with timeout for shutdown
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()

		// Attempt graceful shutdown
		if err := server.Shutdown(ctx); err != nil {
			log.Errorf("Error during server shutdown: %v", err)
			// Force close if graceful shutdown fails
			if err := server.Close(); err != nil {
				return fmt.Errorf("failed to close server: %w", err)
			}
			return fmt.Errorf("server shutdown failed: %w", err)
		}

		log.Infof("Server stopped gracefully")
	}

	return nil
}
