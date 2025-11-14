package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/api"
	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/config"
	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/service"
)

var (
	configPath = flag.String("config", "/etc/keen-pbr/config.json", "Path to configuration file")
	listenAddr = flag.String("listen", ":8080", "Address to listen on")
)

func main() {
	flag.Parse()

	// Load configuration
	cfg, err := config.Load(*configPath)
	if err != nil {
		log.Fatalf("Failed to load config: %v", err)
	}

	// Create service
	svc := service.New()

	// Create API server
	apiServer := api.New(cfg, *configPath, svc)

	// Setup HTTP server
	server := &http.Server{
		Addr:    *listenAddr,
		Handler: apiServer,
	}

	// Handle graceful shutdown
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	go func() {
		<-sigChan
		fmt.Println("\nShutting down...")

		// Stop service if running
		if svc.GetStatus() == service.StatusRunning {
			if err := svc.Stop(); err != nil {
				log.Printf("Error stopping service: %v", err)
			}
		}

		// Save configuration
		if err := cfg.Save(*configPath); err != nil {
			log.Printf("Error saving config: %v", err)
		}

		server.Close()
		os.Exit(0)
	}()

	// Start HTTP server
	fmt.Printf("Starting keen-pbr API server on %s\n", *listenAddr)
	fmt.Printf("Using config file: %s\n", *configPath)

	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatalf("Server error: %v", err)
	}
}
