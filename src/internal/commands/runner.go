package commands

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

// RestartableRunner manages a goroutine that can be restarted on crash.
// It provides crash isolation so that one component crashing doesn't affect others.
type RestartableRunner struct {
	name           string
	runFunc        func(ctx context.Context) error
	mu             sync.RWMutex
	running        bool
	ctx            context.Context
	cancel         context.CancelFunc
	done           chan struct{}
	lastError      error
	restartCount   int
	maxRestarts    int           // 0 means unlimited
	restartBackoff time.Duration // Initial backoff duration
	maxBackoff     time.Duration // Maximum backoff duration
}

// RunnerConfig contains configuration for RestartableRunner.
type RunnerConfig struct {
	Name           string
	MaxRestarts    int           // 0 = unlimited restarts
	RestartBackoff time.Duration // Initial backoff (default: 1s)
	MaxBackoff     time.Duration // Max backoff (default: 30s)
}

// NewRestartableRunner creates a new restartable runner.
func NewRestartableRunner(cfg RunnerConfig, runFunc func(ctx context.Context) error) *RestartableRunner {
	if cfg.RestartBackoff == 0 {
		cfg.RestartBackoff = 1 * time.Second
	}
	if cfg.MaxBackoff == 0 {
		cfg.MaxBackoff = 30 * time.Second
	}

	return &RestartableRunner{
		name:           cfg.Name,
		runFunc:        runFunc,
		maxRestarts:    cfg.MaxRestarts,
		restartBackoff: cfg.RestartBackoff,
		maxBackoff:     cfg.MaxBackoff,
	}
}

// Start starts the runner in a goroutine.
func (r *RestartableRunner) Start(ctx context.Context) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.running {
		return fmt.Errorf("%s is already running", r.name)
	}

	r.ctx, r.cancel = context.WithCancel(ctx)
	r.done = make(chan struct{})
	r.running = true
	r.restartCount = 0
	r.lastError = nil

	go r.runLoop()

	return nil
}

// Stop stops the runner gracefully.
func (r *RestartableRunner) Stop() error {
	r.mu.Lock()
	if !r.running {
		r.mu.Unlock()
		return nil
	}

	cancel := r.cancel
	done := r.done
	r.mu.Unlock()

	if cancel != nil {
		cancel()
	}

	// Wait for the runner to finish
	select {
	case <-done:
		// Runner finished
	case <-time.After(30 * time.Second):
		return fmt.Errorf("%s: timeout waiting for stop", r.name)
	}

	r.mu.Lock()
	r.running = false
	r.mu.Unlock()

	return nil
}

// IsRunning returns true if the runner is currently running.
func (r *RestartableRunner) IsRunning() bool {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.running
}

// LastError returns the last error that occurred.
func (r *RestartableRunner) LastError() error {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.lastError
}

// RestartCount returns the number of restarts that have occurred.
func (r *RestartableRunner) RestartCount() int {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.restartCount
}

// runLoop runs the main loop with crash recovery.
func (r *RestartableRunner) runLoop() {
	defer close(r.done)

	backoff := r.restartBackoff

	for {
		select {
		case <-r.ctx.Done():
			log.Infof("%s: context cancelled, stopping", r.name)
			return
		default:
		}

		// Run the function with panic recovery
		err := r.runWithRecovery()

		r.mu.Lock()
		r.lastError = err
		r.mu.Unlock()

		if err == nil {
			// Clean exit
			log.Infof("%s: exited cleanly", r.name)
			return
		}

		// Check if context is done (intentional shutdown)
		select {
		case <-r.ctx.Done():
			log.Infof("%s: context cancelled during run, stopping", r.name)
			return
		default:
		}

		// Increment restart count
		r.mu.Lock()
		r.restartCount++
		restartCount := r.restartCount
		r.mu.Unlock()

		// Check max restarts
		if r.maxRestarts > 0 && restartCount >= r.maxRestarts {
			log.Errorf("%s: max restarts (%d) reached, giving up. Last error: %v", r.name, r.maxRestarts, err)
			return
		}

		log.Errorf("%s: crashed with error: %v. Restarting in %v (restart #%d)", r.name, err, backoff, restartCount)

		// Wait for backoff duration
		select {
		case <-r.ctx.Done():
			return
		case <-time.After(backoff):
		}

		// Increase backoff for next restart (exponential backoff)
		backoff *= 2
		if backoff > r.maxBackoff {
			backoff = r.maxBackoff
		}
	}
}

// runWithRecovery runs the function and recovers from panics.
func (r *RestartableRunner) runWithRecovery() (err error) {
	defer func() {
		if recovered := recover(); recovered != nil {
			err = fmt.Errorf("panic: %v", recovered)
		}
	}()

	return r.runFunc(r.ctx)
}
