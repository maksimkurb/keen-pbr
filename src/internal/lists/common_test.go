package lists

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
)

func TestAppendDomain(t *testing.T) {
	// Create mock domain store
	domainStore := CreateDomainStore(1)

	// Create mock ipset
	ipsets := []DestIPSet{
		{
			Index: 0,
			Name:  "test",
		},
	}

	tests := []struct {
		name     string
		host     string
		expected bool // whether domain should be added
	}{
		{"Valid domain", "example.com", true},
		{"Empty line", "", false},
		{"Comment line", "#comment", false},
		{"Whitespace only", "   ", false},
		{"Domain with subdomain", "sub.example.com", true},
	}

	initialCount := domainStore.domainsCount

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			isDomain := appendDomain(tt.host, ipsets, domainStore)
			if tt.expected && !isDomain {
				t.Errorf("Expected domain to be added, but it wasn't")
			}
		})
	}

	// Check that valid domains were added
	if domainStore.domainsCount <= initialCount {
		t.Error("Expected domain count to increase")
	}
}

func TestAppendDomain_NilStore(t *testing.T) {
	ipsets := []DestIPSet{
		{Index: 0, Name: "test"},
	}

	isDomain := appendDomain("example.com", ipsets, nil)
	if !isDomain {
		t.Error("Expected domain to be recognized")
	}
}

func TestAppendIPOrCIDR(t *testing.T) {
	// Test IP parsing logic without real networking calls
	ipCount := 0

	tests := []struct {
		name     string
		host     string
		expectIP bool
	}{
		{"Domain name", "example.com", false},
		{"Empty line", "", false},
		{"Comment", "#comment", false},
		{"Invalid IP", "300.300.300.300", false},
	}

	// Use empty ipsets array to avoid networking calls
	emptyIPSets := []DestIPSet{}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			initialCount := ipCount
			isIPv4, isIPv6, err := appendIPOrCIDR(tt.host, emptyIPSets, &ipCount)

			// Should not error for non-IP inputs or empty ipsets
			if err != nil {
				t.Errorf("Unexpected error: %v", err)
			}

			// Count should not change for non-IP inputs
			if ipCount != initialCount {
				t.Errorf("Expected count to remain %d, got %d", initialCount, ipCount)
			}

			// For these test cases, we don't expect IPs
			if isIPv4 || isIPv6 {
				t.Errorf("Expected no IP detection for '%s'", tt.host)
			}
		})
	}
}

func TestIterateOverList_File(t *testing.T) {
	tmpDir := t.TempDir()
	cfg := &config.Config{
		Lists: []*config.ListSource{},
	}

	// Create test file
	testFile := filepath.Join(tmpDir, "test.txt")
	content := `example.com
# comment
test.org

another.com`

	err := os.WriteFile(testFile, []byte(content), 0644)
	if err != nil {
		t.Fatalf("Failed to create test file: %v", err)
	}

	list := &config.ListSource{
		ListName: "test",
		File:     testFile,
	}

	var lines []string
	err = iterateOverList(list, cfg, func(line string) error {
		lines = append(lines, line)
		return nil
	})

	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}

	expectedLines := 5 // 4 content lines + 1 empty line
	if len(lines) != expectedLines {
		t.Errorf("Expected %d lines, got %d", expectedLines, len(lines))
	}

	if lines[0] != "example.com" {
		t.Errorf("Expected first line to be 'example.com', got '%s'", lines[0])
	}
}

func TestIterateOverList_Hosts(t *testing.T) {
	cfg := &config.Config{}

	list := &config.ListSource{
		ListName: "test",
		Hosts:    []string{"example.com", "test.org"},
	}

	var lines []string
	err := iterateOverList(list, cfg, func(line string) error {
		lines = append(lines, line)
		return nil
	})

	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}

	if len(lines) != 2 {
		t.Errorf("Expected 2 lines, got %d", len(lines))
	}

	if lines[0] != "example.com" {
		t.Errorf("Expected first line to be 'example.com', got '%s'", lines[0])
	}
}

func TestIterateOverList_NonExistentFile(t *testing.T) {
	cfg := &config.Config{}

	list := &config.ListSource{
		ListName: "test",
		File:     "/nonexistent/file.txt",
	}

	err := iterateOverList(list, cfg, func(line string) error {
		return nil
	})

	if err == nil {
		t.Error("Expected error for non-existent file")
	}
}

func TestGetListByName_Found(t *testing.T) {
	cfg := &config.Config{
		Lists: []*config.ListSource{
			{ListName: "list1", URL: "http://example.com/1"},
			{ListName: "list2", URL: "http://example.com/2"},
		},
	}

	list, err := getListByName(cfg, "list2")
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}

	if list.ListName != "list2" {
		t.Errorf("Expected list name 'list2', got '%s'", list.ListName)
	}

	if list.URL != "http://example.com/2" {
		t.Errorf("Expected URL 'http://example.com/2', got '%s'", list.URL)
	}
}

func TestGetListByName_NotFound(t *testing.T) {
	cfg := &config.Config{
		Lists: []*config.ListSource{
			{ListName: "list1", URL: "http://example.com/1"},
		},
	}

	_, err := getListByName(cfg, "nonexistent")
	if err == nil {
		t.Error("Expected error for non-existent list")
	}

	expectedMsg := "list \"nonexistent\" not found"
	if err.Error() != expectedMsg {
		t.Errorf("Expected error message '%s', got '%s'", expectedMsg, err.Error())
	}
}
