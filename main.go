package main

import (
	"flag"
	"fmt"
	"log"
	"maksimkurb/keenetic-pbr/lib"
	"os"
	"path/filepath"
)

// Command represents the subcommand to execute
type Command string

const (
	Download Command = "download"
	Apply    Command = "apply"
)

// Config holds the application configuration
type Config struct {
	// Configuration fields will be defined in config package
}

// CLI represents command line arguments
type CLI struct {
	configPath string
	command    Command
}

func parseFlags() *CLI {
	cli := &CLI{}

	// Define flags
	flag.StringVar(&cli.configPath, "config", "/opt/etc/keenetic-pbr/keenetic-pbr.conf", "Path to configuration file")

	// Custom usage message
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Keenetic Policy-Based Routing Manager\n\n")
		fmt.Fprintf(os.Stderr, "Usage: %s [options] <command>\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Commands:\n")
		fmt.Fprintf(os.Stderr, "  download    Download lists specified in config\n")
		fmt.Fprintf(os.Stderr, "  apply       Apply configuration to router\n\n")
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
	}

	flag.Parse()

	// Process command
	args := flag.Args()
	if len(args) != 1 {
		flag.Usage()
		os.Exit(1)
	}

	switch args[0] {
	case "download":
		cli.command = Download
	case "apply":
		cli.command = Apply
	default:
		fmt.Fprintf(os.Stderr, "Unknown command: %s\n", args[0])
		flag.Usage()
		os.Exit(1)
	}

	return cli
}

func init() {
	// Setup logging
	if os.Getenv("LOG_LEVEL") == "" {
		os.Setenv("LOG_LEVEL", "info")
	}
	log.SetFlags(log.LstdFlags | log.Lshortfile)
}

func main() {
	cli := parseFlags()

	// Ensure config directory exists
	configDir := filepath.Dir(cli.configPath)
	if err := os.MkdirAll(configDir, 0755); err != nil {
		log.Fatalf("Failed to create config directory: %v", err)
	}

	// Load configuration
	config, err := lib.LoadConfig(cli.configPath)
	if err != nil {
		log.Fatalf("Failed to load configuration: %v", err)
	}

	// Execute command
	switch cli.command {
	case Download:
		if err := lib.DownloadLists(config); err != nil {
			log.Fatalf("Failed to download lists: %v", err)
		}
	case Apply:
		if err := lib.ApplyLists(config); err != nil {
			log.Fatalf("Failed to apply configuration: %v", err)
		}
	}
}
