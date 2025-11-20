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
	"github.com/maksimkurb/keen-pbr/src/internal/domain"
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
	networkMgr      domain.NetworkManager // Persistent network manager for interface tracking
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

	// Create dependency container and persistent network manager
	// This manager maintains state across the service lifecycle to detect
	// interface changes and prevent unnecessary route updates
	deps := domain.NewDefaultDependencies()
	s.networkMgr = deps.NetworkManager()

	// Initial setup: download lists if not present
	log.Infof("Checking and downloading URL lists if not present...")
	if err := lists.DownloadLists(s.cfg); err != nil {
		log.Warnf("Some lists failed to download: %v", err)
	}

	// Initial setup: create ipsets and fill them
	log.Infof("Importing lists to ipsets...")
	if err := lists.ImportListsToIPSets(s.cfg, deps.ListManager()); err != nil {
		return fmt.Errorf("failed to import lists: %v", err)
	}

	// Apply persistent network configuration (iptables rules and ip rules)
	if err := s.networkMgr.ApplyPersistentConfig(s.cfg.IPSets); err != nil {
		return fmt.Errorf("failed to apply persistent network configuration: %v", err)
	}

	// Apply initial routing (ip routes)
	log.Infof("Applying initial routing configuration...")
	if err := s.networkMgr.ApplyRoutingConfig(s.cfg.IPSets); err != nil {
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

			// Update routing configuration (only if interfaces changed)
			// This uses smart change detection to avoid unnecessary route updates
			log.Debugf("Checking interface states...")
			if _, err := s.networkMgr.UpdateRoutingIfChanged(s.cfg.IPSets); err != nil {
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
	if err := s.networkMgr.ApplyPersistentConfig(s.cfg.IPSets); err != nil {
		return fmt.Errorf("failed to apply persistent network configuration: %v", err)
	}

	// Force update routing configuration (ip routes)
	// SIGHUP forces a full refresh, not just changed interfaces
	log.Infof("Forcing routing configuration update...")
	if err := s.networkMgr.ApplyRoutingConfig(s.cfg.IPSets); err != nil {
		return fmt.Errorf("failed to apply routing configuration: %v", err)
	}

	log.Infof("Configuration rechecked successfully")
	return nil
}

func (s *ServiceCommand) shutdown() error {
	log.Infof("Shutting down keen-pbr service...")

	// Remove all network configuration
	if err := s.networkMgr.UndoConfig(s.cfg.IPSets); err != nil {
		log.Errorf("Failed to undo routing configuration: %v", err)
		return err
	}

	log.Infof("Service stopped successfully")
	return nil
}
