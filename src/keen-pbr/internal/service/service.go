package service

import (
	"context"
	"fmt"
	"sync"
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
	status    Status
	enabled   bool
	ctx       context.Context
	cancel    context.CancelFunc
	mu        sync.RWMutex
	wg        sync.WaitGroup
}

// New creates a new Service instance
func New() *Service {
	return &Service{
		status:  StatusStopped,
		enabled: false,
	}
}

// Start starts the service
func (s *Service) Start() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.status == StatusRunning || s.status == StatusStarting {
		return fmt.Errorf("service is already running or starting")
	}

	s.status = StatusStarting
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

// run is the main service loop
func (s *Service) run() {
	<-s.ctx.Done()
	// Service main loop would go here
	// This would handle PBR operations, DNS resolution, etc.
}
