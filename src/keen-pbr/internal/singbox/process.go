package singbox

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"sync"
	"time"

	"github.com/maksimkurb/keenetic-pbr-go/keen-pbr/internal/config"
)

// ProcessStatus represents the sing-box process status
type ProcessStatus string

const (
	ProcessStatusStopped ProcessStatus = "stopped"
	ProcessStatusRunning ProcessStatus = "running"
	ProcessStatusCrashed ProcessStatus = "crashed"
	ProcessStatusFailed  ProcessStatus = "failed" // Failed to start
)

// ProcessManager manages the sing-box process lifecycle
type ProcessManager struct {
	mu              sync.RWMutex
	cmd             *exec.Cmd
	status          ProcessStatus
	configPath      string
	binaryPath      string
	config          *config.Config
	errorOutput     string
	lastStartTime   time.Time
	crashCallback   func(error) // Called when process crashes
	stdoutBuffer    *bytes.Buffer
	stderrBuffer    *bytes.Buffer
	ctx             context.Context
	cancel          context.CancelFunc
}

// ProcessInfo contains information about the sing-box process
type ProcessInfo struct {
	Status        ProcessStatus `json:"status"`
	ErrorOutput   string        `json:"errorOutput,omitempty"`
	LastStartTime time.Time     `json:"lastStartTime,omitempty"`
	PID           int           `json:"pid,omitempty"`
}

// NewProcessManager creates a new ProcessManager
func NewProcessManager(cfg *config.Config, binaryPath, configPath string) *ProcessManager {
	return &ProcessManager{
		config:       cfg,
		binaryPath:   binaryPath,
		configPath:   configPath,
		status:       ProcessStatusStopped,
		stdoutBuffer: new(bytes.Buffer),
		stderrBuffer: new(bytes.Buffer),
	}
}

// SetCrashCallback sets a callback to be called when sing-box crashes
func (pm *ProcessManager) SetCrashCallback(callback func(error)) {
	pm.mu.Lock()
	defer pm.mu.Unlock()
	pm.crashCallback = callback
}

// Start starts the sing-box process
func (pm *ProcessManager) Start() error {
	pm.mu.Lock()
	defer pm.mu.Unlock()

	if pm.status == ProcessStatusRunning {
		return fmt.Errorf("sing-box is already running")
	}

	// Generate and save configuration
	if err := pm.generateConfigFile(); err != nil {
		pm.status = ProcessStatusFailed
		pm.errorOutput = fmt.Sprintf("Failed to generate config: %v", err)
		return fmt.Errorf("failed to generate config: %w", err)
	}

	// Clear previous error output and buffers
	pm.errorOutput = ""
	pm.stdoutBuffer.Reset()
	pm.stderrBuffer.Reset()

	// Create context for process management
	pm.ctx, pm.cancel = context.WithCancel(context.Background())

	// Create command
	pm.cmd = exec.CommandContext(pm.ctx, pm.binaryPath, "run", "-c", pm.configPath)

	// Capture stdout and stderr
	stdoutPipe, err := pm.cmd.StdoutPipe()
	if err != nil {
		pm.status = ProcessStatusFailed
		pm.errorOutput = fmt.Sprintf("Failed to create stdout pipe: %v", err)
		return fmt.Errorf("failed to create stdout pipe: %w", err)
	}

	stderrPipe, err := pm.cmd.StderrPipe()
	if err != nil {
		pm.status = ProcessStatusFailed
		pm.errorOutput = fmt.Sprintf("Failed to create stderr pipe: %v", err)
		return fmt.Errorf("failed to create stderr pipe: %w", err)
	}

	// Start the process
	if err := pm.cmd.Start(); err != nil {
		pm.status = ProcessStatusFailed
		pm.errorOutput = fmt.Sprintf("Failed to start sing-box: %v", err)
		return fmt.Errorf("failed to start sing-box: %w", err)
	}

	pm.status = ProcessStatusRunning
	pm.lastStartTime = time.Now()
	log.Printf("sing-box started with PID %d", pm.cmd.Process.Pid)

	// Start goroutines to capture output
	go pm.captureOutput(stdoutPipe, pm.stdoutBuffer)
	go pm.captureOutput(stderrPipe, pm.stderrBuffer)

	// Start goroutine to monitor process
	go pm.monitorProcess()

	return nil
}

// Stop stops the sing-box process
func (pm *ProcessManager) Stop() error {
	pm.mu.Lock()
	defer pm.mu.Unlock()

	if pm.status != ProcessStatusRunning {
		return nil // Already stopped
	}

	log.Println("Stopping sing-box process...")

	// Cancel context to signal shutdown
	if pm.cancel != nil {
		pm.cancel()
	}

	// Give process time to gracefully shutdown
	time.Sleep(500 * time.Millisecond)

	// Force kill if still running
	if pm.cmd != nil && pm.cmd.Process != nil {
		if err := pm.cmd.Process.Kill(); err != nil {
			log.Printf("Warning: failed to kill sing-box process: %v", err)
		}
	}

	pm.status = ProcessStatusStopped
	log.Println("sing-box process stopped")

	return nil
}

// GetInfo returns information about the process
func (pm *ProcessManager) GetInfo() ProcessInfo {
	pm.mu.RLock()
	defer pm.mu.RUnlock()

	info := ProcessInfo{
		Status:        pm.status,
		ErrorOutput:   pm.errorOutput,
		LastStartTime: pm.lastStartTime,
	}

	if pm.cmd != nil && pm.cmd.Process != nil {
		info.PID = pm.cmd.Process.Pid
	}

	return info
}

// GetStatus returns the current process status
func (pm *ProcessManager) GetStatus() ProcessStatus {
	pm.mu.RLock()
	defer pm.mu.RUnlock()
	return pm.status
}

// generateConfigFile generates the sing-box config and writes it to disk
func (pm *ProcessManager) generateConfigFile() error {
	// Generate config
	singboxConfig, err := GenerateConfig(pm.config)
	if err != nil {
		return fmt.Errorf("failed to generate sing-box config: %w", err)
	}

	// Marshal to JSON with indentation
	configJSON, err := json.MarshalIndent(singboxConfig, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to marshal config to JSON: %w", err)
	}

	// Ensure config directory exists
	configDir := "/tmp/sing-box"
	if err := os.MkdirAll(configDir, 0755); err != nil {
		return fmt.Errorf("failed to create config directory: %w", err)
	}

	// Write config file
	if err := os.WriteFile(pm.configPath, configJSON, 0644); err != nil {
		return fmt.Errorf("failed to write config file: %w", err)
	}

	log.Printf("sing-box config written to %s", pm.configPath)
	return nil
}

// captureOutput captures output from stdout/stderr pipe
func (pm *ProcessManager) captureOutput(pipe io.ReadCloser, buffer *bytes.Buffer) {
	defer pipe.Close()

	// Use a tee reader to both buffer and log
	teeReader := io.TeeReader(pipe, buffer)
	scanner := io.NopCloser(teeReader)

	// Copy all output
	io.Copy(io.Discard, scanner)
}

// monitorProcess monitors the sing-box process for crashes
func (pm *ProcessManager) monitorProcess() {
	err := pm.cmd.Wait()

	pm.mu.Lock()
	defer pm.mu.Unlock()

	// Check if this was an intentional stop (status already set to stopped)
	if pm.status == ProcessStatusStopped {
		return
	}

	// Process crashed or failed
	exitErr := ""
	if err != nil {
		exitErr = err.Error()
	}

	// Capture stderr output
	stderrOutput := pm.stderrBuffer.String()
	stdoutOutput := pm.stdoutBuffer.String()

	errorMessage := fmt.Sprintf("sing-box process exited unexpectedly.\n")
	if exitErr != "" {
		errorMessage += fmt.Sprintf("Exit error: %s\n", exitErr)
	}
	if stderrOutput != "" {
		errorMessage += fmt.Sprintf("Stderr:\n%s\n", stderrOutput)
	}
	if stdoutOutput != "" {
		errorMessage += fmt.Sprintf("Stdout:\n%s", stdoutOutput)
	}

	pm.errorOutput = errorMessage
	log.Printf("sing-box crashed: %s", errorMessage)

	// Check if process crashed shortly after start (likely a config error)
	timeSinceStart := time.Since(pm.lastStartTime)
	if timeSinceStart < 5*time.Second {
		pm.status = ProcessStatusFailed
		log.Printf("sing-box failed to start (crashed within %v)", timeSinceStart)
	} else {
		pm.status = ProcessStatusCrashed
	}

	// Call crash callback if set
	callback := pm.crashCallback
	pm.mu.Unlock()

	if callback != nil {
		callback(fmt.Errorf(errorMessage))
	}

	pm.mu.Lock()
}
