package domain

import (
	"testing"
)

func TestNewAppDependencies(t *testing.T) {
	t.Run("Default configuration", func(t *testing.T) {
		deps := NewAppDependencies(AppConfig{})

		if deps.KeeneticClient() == nil {
			t.Error("Expected Keenetic client to be created with default config")
		}
		if deps.NetworkManager() == nil {
			t.Error("Expected network manager to be created")
		}
		if deps.IPSetManager() == nil {
			t.Error("Expected ipset manager to be created")
		}
	})

	t.Run("Custom Keenetic URL", func(t *testing.T) {
		deps := NewAppDependencies(AppConfig{
			KeeneticURL: "http://custom.router/rci",
		})

		if deps.KeeneticClient() == nil {
			t.Error("Expected Keenetic client to be created with custom URL")
		}
	})

	t.Run("Disabled Keenetic", func(t *testing.T) {
		deps := NewAppDependencies(AppConfig{
			DisableKeenetic: true,
		})

		if deps.KeeneticClient() != nil {
			t.Error("Expected Keenetic client to be nil when disabled")
		}
		if deps.NetworkManager() == nil {
			t.Error("Expected network manager to still be created")
		}
	})
}

func TestNewDefaultDependencies(t *testing.T) {
	deps := NewDefaultDependencies()

	if deps == nil {
		t.Fatal("Expected dependencies to be created")
	}
	if deps.KeeneticClient() == nil {
		t.Error("Expected Keenetic client to be created with defaults")
	}
	if deps.NetworkManager() == nil {
		t.Error("Expected network manager to be created")
	}
	if deps.IPSetManager() == nil {
		t.Error("Expected ipset manager to be created")
	}
}

func TestManagersReturnSameInstance(t *testing.T) {
	deps := NewDefaultDependencies()

	t.Run("NetworkManager returns same instance", func(t *testing.T) {
		mgr1 := deps.NetworkManager()
		mgr2 := deps.NetworkManager()

		if mgr1 == nil {
			t.Error("Expected network manager to be created")
		}
		if mgr1 != mgr2 {
			t.Error("Expected same network manager instance on multiple calls")
		}
	})

	t.Run("IPSetManager returns same instance", func(t *testing.T) {
		mgr1 := deps.IPSetManager()
		mgr2 := deps.IPSetManager()

		if mgr1 == nil {
			t.Error("Expected ipset manager to be created")
		}
		if mgr1 != mgr2 {
			t.Error("Expected same ipset manager instance on multiple calls")
		}
	})

	t.Run("KeeneticClient returns same instance", func(t *testing.T) {
		client1 := deps.KeeneticClient()
		client2 := deps.KeeneticClient()

		if client1 == nil {
			t.Error("Expected keenetic client to be created")
		}
		if client1 != client2 {
			t.Error("Expected same keenetic client instance on multiple calls")
		}
	})
}
