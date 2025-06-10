package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadConfig_NonExistentFile(t *testing.T) {
	_, err := LoadConfig("/non/existent/file.toml")
	if err == nil {
		t.Error("Expected error for non-existent file")
	}
}

func TestLoadConfig_InvalidTOML(t *testing.T) {
	tmpDir := t.TempDir()
	configFile := filepath.Join(tmpDir, "invalid.toml")

	invalidTOML := `[general
	lists_output_dir = "/tmp"`

	err := os.WriteFile(configFile, []byte(invalidTOML), 0644)
	if err != nil {
		t.Fatalf("Failed to write test file: %v", err)
	}

	_, err = LoadConfig(configFile)
	if err == nil {
		t.Error("Expected error for invalid TOML")
	}
}

func TestLoadConfig_ValidConfig(t *testing.T) {
	tmpDir := t.TempDir()
	configFile := filepath.Join(tmpDir, "valid.toml")

	validTOML := `[general]
lists_output_dir = "/tmp"

[[ipset]]
ipset_name = "test"
lists = ["test_list"]
ip_version = 4

[ipset.routing]
interfaces = ["eth0"]
fwmark = 100
table = 100
priority = 100

[[list]]
list_name = "test_list"
hosts = ["example.com"]`

	err := os.WriteFile(configFile, []byte(validTOML), 0644)
	if err != nil {
		t.Fatalf("Failed to write test file: %v", err)
	}

	config, err := LoadConfig(configFile)
	if err != nil {
		t.Fatalf("Expected no error for valid config: %v", err)
	}

	if config == nil {
		t.Error("Expected config to be non-nil")
	}

	if config.General.ListsOutputDir != "/tmp" {
		t.Errorf("Expected lists_output_dir to be '/tmp', got %s", config.General.ListsOutputDir)
	}
}

func TestLoadConfig_RelativePath(t *testing.T) {
	tmpDir := t.TempDir()
	configFile := filepath.Join(tmpDir, "config.toml")

	validTOML := `[general]
lists_output_dir = "/tmp"`

	err := os.WriteFile(configFile, []byte(validTOML), 0644)
	if err != nil {
		t.Fatalf("Failed to write test file: %v", err)
	}

	oldWd, _ := os.Getwd()
	defer os.Chdir(oldWd)

	os.Chdir(tmpDir)

	_, err = LoadConfig("config.toml")
	if err != nil {
		t.Errorf("Expected no error for relative path: %v", err)
	}
}

func TestSerializeConfig(t *testing.T) {
	config := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp",
		},
	}

	buf, err := config.SerializeConfig()
	if err != nil {
		t.Fatalf("Failed to serialize config: %v", err)
	}

	if buf == nil {
		t.Error("Expected buffer to be non-nil")
	}

	content := buf.String()
	if content == "" {
		t.Error("Expected serialized content to be non-empty")
	}
}

func TestWriteConfig(t *testing.T) {
	tmpDir := t.TempDir()
	configFile := filepath.Join(tmpDir, "test.toml")

	config := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "/tmp",
		},
		_absConfigFilePath: configFile,
	}

	err := config.WriteConfig()
	if err != nil {
		t.Fatalf("Failed to write config: %v", err)
	}

	if _, err := os.Stat(configFile); os.IsNotExist(err) {
		t.Error("Expected config file to exist after writing")
	}
}

func TestUpgradeConfig_DeprecatedInterface(t *testing.T) {
	config := &Config{
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test",
				IPVersion: Ipv4,
				Routing: &RoutingConfig{
					DeprecatedInterface: "eth0",
					Interfaces:          []string{},
				},
			},
		},
	}

	upgraded, err := config.UpgradeConfig()
	if err != nil {
		t.Fatalf("Upgrade failed: %v", err)
	}

	if !upgraded {
		t.Error("Expected config to be upgraded")
	}

	if len(config.IPSets[0].Routing.Interfaces) != 1 || config.IPSets[0].Routing.Interfaces[0] != "eth0" {
		t.Error("Expected deprecated interface to be moved to interfaces array")
	}

	if config.IPSets[0].Routing.DeprecatedInterface != "" {
		t.Error("Expected deprecated interface field to be cleared")
	}
}

func TestUpgradeConfig_DeprecatedLists(t *testing.T) {
	config := &Config{
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test",
				IPVersion: Ipv4,
				DeprecatedLists: []*ListSource{
					{
						ListName: "",
						URL:      "http://example.com",
					},
				},
				Lists: []string{},
				Routing: &RoutingConfig{
					Interfaces: []string{"eth0"},
				},
			},
		},
		Lists: []*ListSource{},
	}

	upgraded, err := config.UpgradeConfig()
	if err != nil {
		t.Fatalf("Upgrade failed: %v", err)
	}

	if !upgraded {
		t.Error("Expected config to be upgraded")
	}

	if len(config.Lists) != 1 {
		t.Error("Expected list to be moved to global lists")
	}

	if len(config.IPSets[0].Lists) != 1 {
		t.Error("Expected list name to be added to ipset lists")
	}

	if config.IPSets[0].DeprecatedLists != nil {
		t.Error("Expected deprecated lists field to be cleared")
	}
}

func TestUpgradeConfig_IPVersion(t *testing.T) {
	config := &Config{
		IPSets: []*IPSetConfig{
			{
				IPSetName: "test",
				IPVersion: 0,
				Routing: &RoutingConfig{
					Interfaces: []string{"eth0"},
				},
			},
		},
	}

	upgraded, err := config.UpgradeConfig()
	if err != nil {
		t.Fatalf("Upgrade failed: %v", err)
	}

	if !upgraded {
		t.Error("Expected config to be upgraded")
	}

	if config.IPSets[0].IPVersion != Ipv4 {
		t.Errorf("Expected IP version to be upgraded to IPv4, got %d", config.IPSets[0].IPVersion)
	}
}

func TestUpgradeConfig_DeprecatedListName(t *testing.T) {
	config := &Config{
		Lists: []*ListSource{
			{
				DeprecatedName: "old_name",
				ListName:       "",
				URL:            "http://example.com",
			},
		},
	}

	upgraded, err := config.UpgradeConfig()
	if err != nil {
		t.Fatalf("Upgrade failed: %v", err)
	}

	if !upgraded {
		t.Error("Expected config to be upgraded")
	}

	if config.Lists[0].ListName != "old_name" {
		t.Error("Expected deprecated name to be moved to list_name")
	}

	if config.Lists[0].DeprecatedName != "" {
		t.Error("Expected deprecated name field to be cleared")
	}
}

func TestExampleConfig(t *testing.T) {
	configFile := filepath.Join("../../keen-pbr.example.conf")

	config, err := LoadConfig(configFile)
	if err != nil {
		t.Fatalf("Expected no error for valid config: %v", err)
	}

	if config == nil {
		t.Error("Expected config to be non-nil")
	}
}
