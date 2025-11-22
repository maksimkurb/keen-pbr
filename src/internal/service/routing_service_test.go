package service

import (
	"errors"
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/mocks"
)

func TestRoutingService_Apply(t *testing.T) {
	t.Run("Full apply", func(t *testing.T) {
		netMgr := mocks.NewMockNetworkManager()
		ipsetMgr := mocks.NewMockIPSetManager()

		service := NewRoutingService(netMgr, ipsetMgr)

		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{
					IPSetName: "test",
					IPVersion: config.Ipv4,
					Routing: &config.RoutingConfig{
						IPRouteTable:   100,
						FwMark:         100,
						IPRulePriority: 100,
						Interfaces:     []string{"lo"},
					},
				},
			},
		}

		err := service.Apply(cfg, ApplyOptions{})
		if err != nil {
			t.Fatalf("Expected no error, got: %v", err)
		}

		// Verify all operations were called
		if ipsetMgr.CreateIfAbsentCalls != 1 {
			t.Error("Expected CreateIfAbsent to be called")
		}
		if netMgr.ApplyPersistentConfigCalls != 1 {
			t.Error("Expected ApplyPersistentConfig to be called")
		}
		if netMgr.ApplyRoutingConfigCalls != 1 {
			t.Error("Expected ApplyRoutingConfig to be called")
		}
	})

	t.Run("Skip ipset creation", func(t *testing.T) {
		netMgr := mocks.NewMockNetworkManager()
		ipsetMgr := mocks.NewMockIPSetManager()

		service := NewRoutingService(netMgr, ipsetMgr)

		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{IPSetName: "test", IPVersion: config.Ipv4},
			},
		}

		err := service.Apply(cfg, ApplyOptions{SkipIPSet: true})
		if err != nil {
			t.Fatalf("Expected no error, got: %v", err)
		}

		if ipsetMgr.CreateIfAbsentCalls != 0 {
			t.Error("Expected CreateIfAbsent NOT to be called when SkipIPSet=true")
		}
	})

	t.Run("Skip routing", func(t *testing.T) {
		netMgr := mocks.NewMockNetworkManager()
		ipsetMgr := mocks.NewMockIPSetManager()

		service := NewRoutingService(netMgr, ipsetMgr)

		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{IPSetName: "test", IPVersion: config.Ipv4},
			},
		}

		err := service.Apply(cfg, ApplyOptions{SkipRouting: true})
		if err != nil {
			t.Fatalf("Expected no error, got: %v", err)
		}

		if netMgr.ApplyPersistentConfigCalls != 0 {
			t.Error("Expected ApplyPersistentConfig NOT to be called when SkipRouting=true")
		}
		if netMgr.ApplyRoutingConfigCalls != 0 {
			t.Error("Expected ApplyRoutingConfig NOT to be called when SkipRouting=true")
		}
	})

	t.Run("Network manager error", func(t *testing.T) {
		expectedErr := errors.New("network error")
		netMgr := &mocks.MockNetworkManager{
			ApplyPersistentConfigFunc: func(ipsets []*config.IPSetConfig) error {
				return expectedErr
			},
		}
		ipsetMgr := mocks.NewMockIPSetManager()

		service := NewRoutingService(netMgr, ipsetMgr)

		cfg := &config.Config{
			IPSets: []*config.IPSetConfig{
				{IPSetName: "test", IPVersion: config.Ipv4},
			},
		}

		err := service.Apply(cfg, ApplyOptions{})
		if err != expectedErr {
			t.Errorf("Expected network error, got: %v", err)
		}
	})
}

func TestRoutingService_FilterIPSetsByInterface(t *testing.T) {
	service := NewRoutingService(nil, nil)

	ipsets := []*config.IPSetConfig{
		{
			IPSetName: "ipset1",
			Routing: &config.RoutingConfig{
				Interfaces: []string{"eth0", "wlan0"},
			},
		},
		{
			IPSetName: "ipset2",
			Routing: &config.RoutingConfig{
				Interfaces: []string{"wlan0", "vpn0"},
			},
		},
		{
			IPSetName: "ipset3",
			Routing: &config.RoutingConfig{
				Interfaces: []string{"eth0"},
			},
		},
		{
			IPSetName: "ipset4",
			Routing:   nil, // No routing config
		},
	}

	t.Run("Filter by eth0", func(t *testing.T) {
		filtered := service.filterIPSetsByInterface(ipsets, "eth0")
		if len(filtered) != 2 {
			t.Errorf("Expected 2 ipsets, got %d", len(filtered))
		}
		if filtered[0].IPSetName != "ipset1" || filtered[1].IPSetName != "ipset3" {
			t.Error("Unexpected filtered ipsets")
		}
	})

	t.Run("Filter by wlan0", func(t *testing.T) {
		filtered := service.filterIPSetsByInterface(ipsets, "wlan0")
		if len(filtered) != 2 {
			t.Errorf("Expected 2 ipsets, got %d", len(filtered))
		}
	})

	t.Run("Filter by non-existent", func(t *testing.T) {
		filtered := service.filterIPSetsByInterface(ipsets, "nonexistent")
		if len(filtered) != 0 {
			t.Errorf("Expected 0 ipsets, got %d", len(filtered))
		}
	})
}

func TestRoutingService_Undo(t *testing.T) {
	netMgr := mocks.NewMockNetworkManager()
	service := NewRoutingService(netMgr, nil)

	cfg := &config.Config{
		IPSets: []*config.IPSetConfig{
			{IPSetName: "test"},
		},
	}

	err := service.Undo(cfg)
	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}

	if netMgr.UndoConfigCalls != 1 {
		t.Error("Expected UndoConfig to be called")
	}
}

func TestRoutingService_UpdateRouting(t *testing.T) {
	netMgr := mocks.NewMockNetworkManager()
	service := NewRoutingService(netMgr, nil)

	cfg := &config.Config{
		IPSets: []*config.IPSetConfig{
			{IPSetName: "test"},
		},
	}

	err := service.UpdateRouting(cfg)
	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}

	if netMgr.ApplyRoutingConfigCalls != 1 {
		t.Error("Expected ApplyRoutingConfig to be called")
	}
	if netMgr.ApplyPersistentConfigCalls != 0 {
		t.Error("Expected ApplyPersistentConfig NOT to be called during update")
	}
}

func TestRoutingService_ApplyPersistentOnly(t *testing.T) {
	netMgr := mocks.NewMockNetworkManager()
	ipsetMgr := mocks.NewMockIPSetManager()
	service := NewRoutingService(netMgr, ipsetMgr)

	cfg := &config.Config{
		IPSets: []*config.IPSetConfig{
			{IPSetName: "test", IPVersion: config.Ipv4},
		},
	}

	err := service.ApplyPersistentOnly(cfg)
	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}

	if ipsetMgr.CreateIfAbsentCalls != 1 {
		t.Error("Expected CreateIfAbsent to be called")
	}
	if netMgr.ApplyPersistentConfigCalls != 1 {
		t.Error("Expected ApplyPersistentConfig to be called")
	}
	if netMgr.ApplyRoutingConfigCalls != 0 {
		t.Error("Expected ApplyRoutingConfig NOT to be called in persistent-only mode")
	}
}
