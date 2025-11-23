package main

import (
	"errors"
	"flag"
	"fmt"
	"os"

	"github.com/maksimkurb/keen-pbr/src/internal/commands"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

var (
	version = "dev"
	commit  = "n/a"
	date    = "n/a"
)

func main() {
	ctx := &commands.AppContext{}

	// Define flags
	flag.StringVar(&ctx.ConfigPath, "config", "/opt/etc/keen-pbr/keen-pbr.conf", "Path to configuration file")
	flag.BoolVar(&ctx.Verbose, "verbose", false, "Enable debug logging")

	// Custom usage message
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Keenetic Policy-Based Routing Manager\n")
		fmt.Fprintf(os.Stderr, "Version: %s (Commit: %s, Date: %s)\n\n", version, commit, date)
		fmt.Fprintf(os.Stderr, "Usage: %s [options] <command>\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Commands:\n")
		fmt.Fprintf(os.Stderr, "  service                 Run as a service/daemon (includes API server, DNS proxy, and routing)\n")
		fmt.Fprintf(os.Stderr, "  download                Download remote lists to lists.d directory\n")
		fmt.Fprintf(os.Stderr, "  apply                   Import IPs/CIDRs from lists to ipsets\n")
		fmt.Fprintf(os.Stderr, "  interfaces              Get available interfaces list\n")
		fmt.Fprintf(os.Stderr, "  self-check              Run self-check\n")
		fmt.Fprintf(os.Stderr, "  undo-routing            Undo any routing configuration (reverts \"apply\" command)\n")
		fmt.Fprintf(os.Stderr, "  dns                     Show System DNS proxy profile\n")
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
	}

	flag.Parse()

	if ctx.Verbose {
		log.SetVerbose(true)
	}

	// Ensure cfg file exists
	if _, err := os.Stat(ctx.ConfigPath); errors.Is(err, os.ErrNotExist) {
		log.Fatalf("Configuration file not found: %s", ctx.ConfigPath)
	}

	// Get interfaces list
	var err error
	if ctx.Interfaces, err = networking.GetInterfaceList(); err != nil {
		log.Fatalf("Failed to get interfaces list: %v", err)
	}

	cmds := []commands.Runner{
		commands.CreateServiceCommand(),
		commands.CreateDownloadCommand(),
		commands.CreateApplyCommand(),
		commands.CreateInterfacesCommand(),
		commands.CreateSelfCheckCommand(),
		commands.CreateUndoCommand(),
		commands.CreateUpgradeConfigCommand(),
		commands.CreateDNSCommand(),
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
