package main

import (
	"flag"
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/commands"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/networking"
	"os"
	"path/filepath"
)

func main() {
	ctx := &commands.AppContext{}

	// Define flags
	flag.StringVar(&ctx.ConfigPath, "config", "/opt/etc/keenetic-pbr/keenetic-pbr.conf", "Path to configuration file")
	flag.BoolVar(&ctx.Verbose, "verbose", false, "Enable debug logging")

	// Custom usage message
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Keenetic Policy-Based Routing Manager\n\n")
		fmt.Fprintf(os.Stderr, "Usage: %s [options] <command>\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Commands:\n")
		fmt.Fprintf(os.Stderr, "  download                Download remote lists to lists.d directory\n")
		fmt.Fprintf(os.Stderr, "  apply                   Import IPs from lists to ipsets and update dnsmasq domains config\n")
		fmt.Fprintf(os.Stderr, "  interfaces              Get available interfaces list\n")
		fmt.Fprintf(os.Stderr, "  self-check              Run self-check\n")
		fmt.Fprintf(os.Stderr, "  undo-routing            Undo any routing configuration (reverts \"apply\" command)\n")
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
	}

	flag.Parse()

	if ctx.Verbose {
		log.SetVerbose(true)
	}

	// Ensure cfg directory exists
	configDir := filepath.Dir(ctx.ConfigPath)
	if err := os.MkdirAll(configDir, 0755); err != nil {
		log.Fatalf("Failed to create cfg directory: %v", err)
	}

	// Get interfaces list
	var err error
	if ctx.Interfaces, err = networking.GetInterfaceList(); err != nil {
		log.Fatalf("Failed to get interfaces list: %v", err)
	}

	cmds := []commands.Runner{
		commands.CreateDownloadCommand(),
		commands.CreateApplyCommand(),
		commands.CreateInterfacesCommand(),
		commands.CreateSelfCheckCommand(),
		commands.CreateUndoCommand(),
		commands.CreateUpgradeConfigCommand(),
	}

	args := flag.Args()

	if len(args) < 1 {
		flag.Usage()
		os.Exit(1)
	}

	subcommand := args[0]
	for _, cmd := range cmds {
		if cmd.Name() == subcommand {
			if err := cmd.Init(args[1:], ctx); err != nil {
				log.Fatalf("Failed to initialize command: %v", err)
			}

			if err := cmd.Run(); err != nil {
				log.Fatalf("Failed to run command: %v", err)
			}

			os.Exit(0)
		}
	}

	log.Fatalf("Unknown subcommand: %s", subcommand)
}
