package log

import (
	"fmt"
	"os"
)

const (
	levelDebug = iota
	levelInfo
	levelWarn
	levelError
)

var (
	verbose     = false
	logPrefixes = map[int]string{
		levelDebug: "\033[37m[DBG]\033[0m", // White
		levelInfo:  "\033[36m[INF]\033[0m", // Cyan
		levelWarn:  "\033[33m[WRN]\033[0m", // Yellow
		levelError: "\033[31m[ERR]\033[0m", // Red
	}
)

// SetVerbose sets the logging verbosity. If true, all log levels are displayed.
func SetVerbose(v bool) {
	verbose = v
}

// Debugf logs a debug message if verbose is true.
func Debugf(format string, args ...interface{}) {
	if verbose {
		logMessage(levelDebug, format, args...)
	}
}

// Infof logs an info message.
func Infof(format string, args ...interface{}) {
	logMessage(levelInfo, format, args...)
}

// Warnf logs a warning message.
func Warnf(format string, args ...interface{}) {
	logMessage(levelWarn, format, args...)
}

// Errorf logs an error message.
func Errorf(format string, args ...interface{}) {
	logMessage(levelError, format, args...)
}

// Errorf logs an error message.
func Fatalf(format string, args ...interface{}) {
	logMessage(levelError, format, args...)
	os.Exit(1)
}

// logMessage formats and writes a log message with the specified log level.
func logMessage(level int, format string, args ...interface{}) {
	prefix := logPrefixes[level]
	message := fmt.Sprintf(format, args...)
	output := prefix + " " + message + "\n"

	// Write the output to the appropriate stream
	if level == levelError {
		os.Stderr.WriteString(output)
	} else {
		os.Stdout.WriteString(output)
	}
}
