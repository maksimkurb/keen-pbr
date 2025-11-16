package config

import (
	"os"
	"path/filepath"
	"testing"
)


func TestConfig_GetAbsDownloadedListsDir(t *testing.T) {
	config := &Config{
		General: &GeneralConfig{
			ListsOutputDir: "lists",
		},
		_absConfigFilePath: "/home/user/config/keen-pbr.toml",
	}
	
	result := config.GetAbsDownloadedListsDir()
	expected := "/home/user/config/lists"
	
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}

func TestListSource_Type(t *testing.T) {
	tests := []struct {
		name     string
		list     *ListSource
		expected string
	}{
		{
			name:     "URL type",
			list:     &ListSource{URL: "http://example.com"},
			expected: "url",
		},
		{
			name:     "Empty defaults to hosts",
			list:     &ListSource{},
			expected: "hosts",
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result := tt.list.Type()
			if result != tt.expected {
				t.Errorf("Expected %s, got %s", tt.expected, result)
			}
		})
	}
}

func TestListSource_Name(t *testing.T) {
	list := &ListSource{ListName: "new_name", DeprecatedName: "old_name"}
	result := list.Name()
	if result != "new_name" {
		t.Errorf("Expected priority field to be used, got %s", result)
	}
}

func TestListSource_GetAbsolutePath(t *testing.T) {
	tmpDir := t.TempDir()
	config := &Config{
		General: &GeneralConfig{
			ListsOutputDir: tmpDir,
		},
		_absConfigFilePath: filepath.Join(tmpDir, "config.toml"),
	}
	
	tests := []struct {
		name        string
		list        *ListSource
		expectError bool
		expectedEnd string
	}{
		{
			name:        "URL list",
			list:        &ListSource{ListName: "test", URL: "http://example.com"},
			expectError: false,
			expectedEnd: "test.lst",
		},
		{
			name:        "File list",
			list:        &ListSource{File: "local.txt"},
			expectError: false,
			expectedEnd: "local.txt",
		},
		{
			name:        "Hosts list error",
			list:        &ListSource{Hosts: []string{"example.com"}},
			expectError: true,
		},
		{
			name:        "Empty list error",
			list:        &ListSource{},
			expectError: true,
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := tt.list.GetAbsolutePath(config)
			
			if tt.expectError {
				if err == nil {
					t.Error("Expected error but got none")
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error but got: %v", err)
				}
				if !filepath.IsAbs(result) {
					t.Errorf("Expected absolute path, got: %s", result)
				}
				if tt.expectedEnd != "" && filepath.Base(result) != tt.expectedEnd {
					t.Errorf("Expected path to end with %s, got: %s", tt.expectedEnd, result)
				}
			}
		})
	}
}

func TestListSource_GetAbsolutePathAndCheckExists(t *testing.T) {
	tmpDir := t.TempDir()
	config := &Config{
		General: &GeneralConfig{
			ListsOutputDir: tmpDir,
		},
		_absConfigFilePath: filepath.Join(tmpDir, "config.toml"),
	}
	
	// Create a test file
	testFile := filepath.Join(tmpDir, "existing.txt")
	err := os.WriteFile(testFile, []byte("test"), 0644)
	if err != nil {
		t.Fatalf("Failed to create test file: %v", err)
	}
	
	tests := []struct {
		name        string
		list        *ListSource
		expectError bool
		errorSubstr string
	}{
		{
			name:        "Existing file",
			list:        &ListSource{File: "existing.txt"},
			expectError: false,
		},
		{
			name:        "Non-existing file",
			list:        &ListSource{File: "nonexistent.txt"},
			expectError: true,
			errorSubstr: "not exists",
		},
		{
			name:        "Non-existing URL list",
			list:        &ListSource{ListName: "test", URL: "http://example.com"},
			expectError: true,
			errorSubstr: "please run 'keen-pbr download' first",
		},
		{
			name:        "Hosts list error",
			list:        &ListSource{Hosts: []string{"example.com"}},
			expectError: true,
			errorSubstr: "not a file",
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result, err := tt.list.GetAbsolutePathAndCheckExists(config)
			
			if tt.expectError {
				if err == nil {
					t.Error("Expected error but got none")
				}
				if tt.errorSubstr != "" && err != nil {
					if !containsSubstring(err.Error(), tt.errorSubstr) {
						t.Errorf("Expected error to contain '%s', got: %v", tt.errorSubstr, err)
					}
				}
			} else {
				if err != nil {
					t.Errorf("Expected no error but got: %v", err)
				}
				if result == "" {
					t.Error("Expected non-empty result")
				}
			}
		})
	}
}

func containsSubstring(s, substr string) bool {
	return len(s) >= len(substr) && (s == substr || 
		len(s) > len(substr) && (s[:len(substr)] == substr || 
		s[len(s)-len(substr):] == substr || 
		containsSubstring(s[1:], substr)))
}