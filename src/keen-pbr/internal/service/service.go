package service

import (
	"context"
	"fmt"
	"log"
	"sync"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/config"
	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/networking"
	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/singbox"
)

// Status represents the service status
type Status string

const (
	StatusStopped Status = "stopped"
	StatusRunning Status = "running"
	StatusStarting Status = "starting"
	StatusStopping Status = "stopping"
)

// Service manages the PBR service lifecycle
type Service struct {
	status          Status
	enabled         bool
	ctx             context.Context
	cancel          context.CancelFunc
	mu              sync.RWMutex
	wg              sync.WaitGroup
	networkManager  *networking.NetworkManager
	singboxManager  *singbox.ProcessManager
	config          *config.Config
	singboxError    string
}

// New creates a new Service instance
func New(cfg *config.Config) *Service {
	// Get sing-box paths from settings
	settings := cfg.GetGeneralSettings()
	binaryPath := "/usr/local/bin/sing-box"
	if settings != nil && settings.SingBoxPath != "" {
		binaryPath = settings.SingBoxPath
	}
	configPath := singbox.ConfigPath

	singboxManager := singbox.NewProcessManager(cfg, binaryPath, configPath)

	svc := &Service{
		status:         StatusStopped,
		enabled:        false,
		networkManager: networking.NewNetworkManager(),
		singboxManager: singboxManager,
		config:         cfg,
	}

	// Set crash callback to stop service when sing-box crashes
	singboxManager.SetCrashCallback(func(err error) {
		log.Printf("sing-box crashed, stopping service: %v", err)
		svc.mu.Lock()
		svc.singboxError = err.Error()
		svc.mu.Unlock()

		// Trigger service stop
		go svc.Stop()
	})

	return svc
}

// Start starts the service
func (s *Service) Start() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.status == StatusRunning || s.status == StatusStarting {
		return fmt.Errorf("service is already running or starting")
	}

	s.status = StatusStarting
	s.singboxError = "" // Clear previous errors

	// Setup networking (ipsets and iptables)
	log.Println("Setting up networking rules...")
	if err := s.networkManager.Setup(); err != nil {
		s.status = StatusStopped
		return fmt.Errorf("failed to setup networking: %w", err)
	}
	log.Println("Networking rules applied successfully")

	// Start sing-box process
	log.Println("Starting sing-box...")
	if err := s.singboxManager.Start(); err != nil {
		// sing-box failed to start, cleanup networking
		log.Printf("Failed to start sing-box: %v", err)
		s.networkManager.Teardown()
		s.status = StatusStopped
		s.singboxError = err.Error()
		return fmt.Errorf("failed to start sing-box: %w", err)
	}
	log.Println("sing-box started successfully")

	s.ctx, s.cancel = context.WithCancel(context.Background())

	// Start service goroutines here
	s.wg.Add(1)
	go func() {
		defer s.wg.Done()
		s.run()
	}()

	s.status = StatusRunning
	return nil
}

// Stop stops the service
func (s *Service) Stop() error {
	s.mu.Lock()
	if s.status != StatusRunning {
		s.mu.Unlock()
		return fmt.Errorf("service is not running")
	}

	s.status = StatusStopping
	cancel := s.cancel
	s.mu.Unlock()

	if cancel != nil {
		cancel()
	}

	s.wg.Wait()

	// Stop sing-box
	log.Println("Stopping sing-box...")
	if err := s.singboxManager.Stop(); err != nil {
		log.Printf("Warning: failed to stop sing-box: %v", err)
	} else {
		log.Println("sing-box stopped successfully")
	}

	// Cleanup temporary files
	log.Println("Cleaning up temporary files...")
	if err := singbox.CleanupTempFiles(); err != nil {
		log.Printf("Warning: failed to cleanup temp files: %v", err)
	} else {
		log.Println("Temporary files cleaned up successfully")
	}

	// Teardown networking (remove iptables rules and ipsets)
	log.Println("Tearing down networking rules...")
	if err := s.networkManager.Teardown(); err != nil {
		log.Printf("Warning: failed to teardown networking: %v", err)
	} else {
		log.Println("Networking rules removed successfully")
	}

	s.mu.Lock()
	s.status = StatusStopped
	s.mu.Unlock()

	return nil
}

// Restart restarts the service
func (s *Service) Restart() error {
	if err := s.Stop(); err != nil && s.GetStatus() != StatusStopped {
		return fmt.Errorf("failed to stop service: %w", err)
	}
	return s.Start()
}

// Enable enables the service (for autostart)
func (s *Service) Enable() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.enabled = true
}

// Disable disables the service (prevent autostart)
func (s *Service) Disable() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.enabled = false
}

// GetStatus returns the current service status
func (s *Service) GetStatus() Status {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.status
}

// IsEnabled returns whether the service is enabled
func (s *Service) IsEnabled() bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.enabled
}

// GetSingboxProcessInfo returns information about the sing-box process
func (s *Service) GetSingboxProcessInfo() singbox.ProcessInfo {
	return s.singboxManager.GetInfo()
}

// run is the main service loop
func (s *Service) run() {
	<-s.ctx.Done()
	// Service main loop would go here
	// This would handle PBR operations, DNS resolution, etc.
}
