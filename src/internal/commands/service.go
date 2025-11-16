package commands

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/lists"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/networking"
)

func CreateServiceCommand() *ServiceCommand {
	sc := &ServiceCommand{
		fs: flag.NewFlagSet("service", flag.ExitOnError),
	}

	sc.fs.IntVar(&sc.MonitorInterval, "monitor-interval", 10, "Interval in seconds to monitor interface changes")

	return sc
}

type ServiceCommand struct {
	fs              *flag.FlagSet
	cfg             *config.Config
	ctx             *AppContext
	MonitorInterval int
}

func (s *ServiceCommand) Name() string {
	return s.fs.Name()
}

func (s *ServiceCommand) Init(args []string, ctx *AppContext) error {
	s.ctx = ctx

	if err := s.fs.Parse(args); err != nil {
		return err
	}

	if cfg, err := loadAndValidateConfigOrFail(ctx.ConfigPath); err != nil {
		return err
	} else {
		s.cfg = cfg
	}

	if err := networking.ValidateInterfacesArePresent(s.cfg, ctx.Interfaces); err != nil {
		return fmt.Errorf("failed to validate interfaces: %v", err)
	}

	return nil
}

func (s *ServiceCommand) Run() error {
	log.Infof("Starting keen-pbr service...")

	// Create context for graceful shutdown
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Setup signal handling
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM, syscall.SIGHUP)

	// Initial setup: create ipsets and fill them
	log.Infof("Importing lists to ipsets...")
	if err := lists.ImportListsToIPSets(s.cfg); err != nil {
		return fmt.Errorf("failed to import lists: %v", err)
	}

	// Apply persistent network configuration (iptables rules and ip rules)
	log.Infof("Applying persistent network configuration (iptables rules and ip rules)...")
	if err := networking.ApplyPersistentNetworkConfiguration(s.cfg); err != nil {
		return fmt.Errorf("failed to apply persistent network configuration: %v", err)
	}

	// Apply initial routing (ip routes)
	log.Infof("Applying initial routing configuration...")
	if err := networking.ApplyRoutingConfiguration(s.cfg); err != nil {
		return fmt.Errorf("failed to apply routing configuration: %v", err)
	}

	log.Infof("Service started successfully. Monitoring interface changes every %d seconds...", s.MonitorInterval)

	// Start monitoring loop
	ticker := time.NewTicker(time.Duration(s.MonitorInterval) * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			log.Infof("Context cancelled, shutting down...")
			return s.shutdown()

		case sig := <-sigChan:
			switch sig {
			case syscall.SIGHUP:
				log.Infof("Received SIGHUP signal, rechecking configuration...")
				if err := s.recheckConfiguration(); err != nil {
					log.Errorf("Failed to recheck configuration: %v", err)
				}
			case syscall.SIGINT, syscall.SIGTERM:
				log.Infof("Received signal %v, shutting down...", sig)
				cancel()
				return s.shutdown()
			}

		case <-ticker.C:
			// Update interface list
			var err error
			if s.ctx.Interfaces, err = networking.GetInterfaceList(); err != nil {
				log.Errorf("Failed to get interfaces list: %v", err)
				continue
			}

			// Update routing configuration (only ip routes)
			log.Debugf("Checking interface states and updating routes...")
			if err := networking.ApplyRoutingConfiguration(s.cfg); err != nil {
				log.Errorf("Failed to update routing configuration: %v", err)
			}
		}
	}
}

func (s *ServiceCommand) recheckConfiguration() error {
	log.Infof("Rechecking configuration (triggered by SIGHUP)...")

	// Update interface list
	var err error
	if s.ctx.Interfaces, err = networking.GetInterfaceList(); err != nil {
		return fmt.Errorf("failed to get interfaces list: %v", err)
	}

	// Reapply persistent network configuration (iptables rules and ip rules)
	log.Infof("Reapplying persistent network configuration...")
	if err := networking.ApplyPersistentNetworkConfiguration(s.cfg); err != nil {
		return fmt.Errorf("failed to apply persistent network configuration: %v", err)
	}

	// Update routing configuration (ip routes)
	log.Infof("Updating routing configuration...")
	if err := networking.ApplyRoutingConfiguration(s.cfg); err != nil {
		return fmt.Errorf("failed to apply routing configuration: %v", err)
	}

	log.Infof("Configuration rechecked successfully")
	return nil
}

func (s *ServiceCommand) shutdown() error {
	log.Infof("Shutting down keen-pbr service...")

	// Remove all network configuration (iptables rules, ip rules, and ip routes)
	for _, ipset := range s.cfg.IPSets {
		if err := undoIpset(ipset); err != nil {
			log.Errorf("Failed to undo routing configuration for ipset [%s]: %v", ipset.IPSetName, err)
			return err
		}
	}

	log.Infof("Service stopped successfully")
	return nil
}
