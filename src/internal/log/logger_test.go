package log

import (
	"bytes"
	"io"
	"os"
	"strings"
	"testing"
)

// Helper to capture output from os.Stdout and os.Stderr
func captureOutput(f func()) (stdout, stderr string) {
	oldStdout := os.Stdout
	oldStderr := os.Stderr

	// Create pipes
	rOut, wOut, _ := os.Pipe()
	rErr, wErr, _ := os.Pipe()

	os.Stdout = wOut
	os.Stderr = wErr

	// Channel to collect output
	outCh := make(chan string)
	errCh := make(chan string)

	// Start goroutines to read from pipes
	go func() {
		var buf bytes.Buffer
		io.Copy(&buf, rOut)
		outCh <- buf.String()
	}()

	go func() {
		var buf bytes.Buffer
		io.Copy(&buf, rErr)
		errCh <- buf.String()
	}()

	// Execute function
	f()

	// Close write ends
	wOut.Close()
	wErr.Close()

	// Get results
	stdout = <-outCh
	stderr = <-errCh

	// Restore original
	os.Stdout = oldStdout
	os.Stderr = oldStderr

	return stdout, stderr
}

func TestSetVerbose(t *testing.T) {
	// Save original state
	originalVerbose := verbose
	defer func() { verbose = originalVerbose }()

	// Test setting to true
	SetVerbose(true)
	if !verbose {
		t.Error("Expected verbose to be true")
	}

	// Test setting to false
	SetVerbose(false)
	if verbose {
		t.Error("Expected verbose to be false")
	}
}

func TestDebugf_VerboseOff(t *testing.T) {
	// Save original state
	originalVerbose := verbose
	defer func() { verbose = originalVerbose }()

	SetVerbose(false)

	stdout, stderr := captureOutput(func() {
		Debugf("test debug message")
	})

	if stdout != "" {
		t.Errorf("Expected no stdout output when verbose is off, got: %s", stdout)
	}

	if stderr != "" {
		t.Errorf("Expected no stderr output when verbose is off, got: %s", stderr)
	}
}

func TestDebugf_VerboseOn(t *testing.T) {
	// Save original state
	originalVerbose := verbose
	defer func() { verbose = originalVerbose }()

	SetVerbose(true)

	stdout, stderr := captureOutput(func() {
		Debugf("test debug message")
	})

	if !strings.Contains(stdout, "[DBG]") {
		t.Errorf("Expected debug message in stdout, got: %s", stdout)
	}

	if !strings.Contains(stdout, "test debug message") {
		t.Errorf("Expected message content in stdout, got: %s", stdout)
	}

	if stderr != "" {
		t.Errorf("Expected no stderr output for debug, got: %s", stderr)
	}
}

func TestInfof(t *testing.T) {
	stdout, stderr := captureOutput(func() {
		Infof("test info message")
	})

	if !strings.Contains(stdout, "[INF]") {
		t.Errorf("Expected info message in stdout, got: %s", stdout)
	}

	if !strings.Contains(stdout, "test info message") {
		t.Errorf("Expected message content in stdout, got: %s", stdout)
	}

	if stderr != "" {
		t.Errorf("Expected no stderr output for info, got: %s", stderr)
	}
}

func TestWarnf(t *testing.T) {
	stdout, stderr := captureOutput(func() {
		Warnf("test warning message")
	})

	if !strings.Contains(stdout, "[WRN]") {
		t.Errorf("Expected warning message in stdout, got: %s", stdout)
	}

	if !strings.Contains(stdout, "test warning message") {
		t.Errorf("Expected message content in stdout, got: %s", stdout)
	}

	if stderr != "" {
		t.Errorf("Expected no stderr output for warning, got: %s", stderr)
	}
}

func TestErrorf(t *testing.T) {
	stdout, stderr := captureOutput(func() {
		Errorf("test error message")
	})

	if stdout != "" {
		t.Errorf("Expected no stdout output for error, got: %s", stdout)
	}

	if !strings.Contains(stderr, "[ERR]") {
		t.Errorf("Expected error message in stderr, got: %s", stderr)
	}

	if !strings.Contains(stderr, "test error message") {
		t.Errorf("Expected message content in stderr, got: %s", stderr)
	}
}

func TestForceStdErr(t *testing.T) {
	// Save original state
	originalForceStdErr := forceStdErr
	defer func() { forceStdErr = originalForceStdErr }()

	SetForceStdErr(true)

	stdout, stderr := captureOutput(func() {
		Infof("test info to stderr")
	})

	if stdout != "" {
		t.Errorf("Expected no stdout output when forceStdErr is true, got: %s", stdout)
	}

	if !strings.Contains(stderr, "[INF]") {
		t.Errorf("Expected info message in stderr when forceStdErr is true, got: %s", stderr)
	}

	if !strings.Contains(stderr, "test info to stderr") {
		t.Errorf("Expected message content in stderr, got: %s", stderr)
	}
}

func TestLogMessage_FormattingWithArgs(t *testing.T) {
	stdout, _ := captureOutput(func() {
		Infof("test message with %s and %d", "string", 42)
	})

	if !strings.Contains(stdout, "test message with string and 42") {
		t.Errorf("Expected formatted message, got: %s", stdout)
	}
}

func TestLogPrefixes(t *testing.T) {
	// Save original state
	originalVerbose := verbose
	defer func() { verbose = originalVerbose }()

	SetVerbose(true)

	tests := []struct {
		name     string
		logFunc  func(string, ...interface{})
		expected string
	}{
		{"Debug", Debugf, "[DBG]"},
		{"Info", Infof, "[INF]"},
		{"Warn", Warnf, "[WRN]"},
		{"Error", Errorf, "[ERR]"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			stdout, stderr := captureOutput(func() {
				tt.logFunc("test message")
			})

			output := stdout + stderr
			if !strings.Contains(output, tt.expected) {
				t.Errorf("Expected prefix %s in output, got: %s", tt.expected, output)
			}
		})
	}
}
