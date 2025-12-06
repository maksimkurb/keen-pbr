package lists

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"
)

// Mock checksum provider for testing
type mockChecksumProvider struct {
	checksum    string
	shouldError bool
}

func (m *mockChecksumProvider) GetChecksum() (string, error) {
	if m.shouldError {
		return "", fmt.Errorf("mock checksum error")
	}
	return m.checksum, nil
}

func TestIsFileChanged_FileNotExists(t *testing.T) {
	mock := &mockChecksumProvider{checksum: "abc123"}
	
	changed, err := IsFileChanged(mock, "/nonexistent/file.txt")
	if err != nil {
		t.Errorf("Expected no error for non-existent file, got: %v", err)
	}
	
	if !changed {
		t.Error("Expected file to be considered changed when it doesn't exist")
	}
}

func TestIsFileChanged_ChecksumError(t *testing.T) {
	tmpDir := t.TempDir()
	testFile := filepath.Join(tmpDir, "test.txt")
	
	// Create test file
	err := os.WriteFile(testFile, []byte("content"), 0644)
	if err != nil {
		t.Fatalf("Failed to create test file: %v", err)
	}
	
	mock := &mockChecksumProvider{shouldError: true}
	
	_, err = IsFileChanged(mock, testFile)
	if err == nil {
		t.Error("Expected error when checksum provider fails")
	}
}

func TestIsFileChanged_NoChecksumFile(t *testing.T) {
	tmpDir := t.TempDir()
	testFile := filepath.Join(tmpDir, "test.txt")
	
	// Create test file but no checksum file
	err := os.WriteFile(testFile, []byte("content"), 0644)
	if err != nil {
		t.Fatalf("Failed to create test file: %v", err)
	}
	
	mock := &mockChecksumProvider{checksum: "abc123"}
	
	changed, err := IsFileChanged(mock, testFile)
	if err != nil {
		t.Errorf("Expected no error when checksum file doesn't exist, got: %v", err)
	}
	
	if !changed {
		t.Error("Expected file to be considered changed when checksum file doesn't exist")
	}
}

func TestIsFileChanged_ChecksumMatches(t *testing.T) {
	tmpDir := t.TempDir()
	testFile := filepath.Join(tmpDir, "test.txt")
	checksumFile := testFile + ".md5"
	
	// Create test file and matching checksum file
	err := os.WriteFile(testFile, []byte("content"), 0644)
	if err != nil {
		t.Fatalf("Failed to create test file: %v", err)
	}
	
	checksum := "abc123"
	err = os.WriteFile(checksumFile, []byte(checksum), 0644)
	if err != nil {
		t.Fatalf("Failed to create checksum file: %v", err)
	}
	
	mock := &mockChecksumProvider{checksum: checksum}
	
	changed, err := IsFileChanged(mock, testFile)
	if err != nil {
		t.Errorf("Unexpected error: %v", err)
	}
	
	if changed {
		t.Error("Expected file to be considered unchanged when checksums match")
	}
}

func TestIsFileChanged_ChecksumDiffers(t *testing.T) {
	tmpDir := t.TempDir()
	testFile := filepath.Join(tmpDir, "test.txt")
	checksumFile := testFile + ".md5"
	
	// Create test file and different checksum file
	err := os.WriteFile(testFile, []byte("content"), 0644)
	if err != nil {
		t.Fatalf("Failed to create test file: %v", err)
	}
	
	err = os.WriteFile(checksumFile, []byte("old_checksum"), 0644)
	if err != nil {
		t.Fatalf("Failed to create checksum file: %v", err)
	}
	
	mock := &mockChecksumProvider{checksum: "new_checksum"}
	
	changed, err := IsFileChanged(mock, testFile)
	if err != nil {
		t.Errorf("Unexpected error: %v", err)
	}
	
	if !changed {
		t.Error("Expected file to be considered changed when checksums differ")
	}
}

func TestReadChecksum_Success(t *testing.T) {
	tmpDir := t.TempDir()
	checksumFile := filepath.Join(tmpDir, "test.md5")
	
	expectedChecksum := "abc123def456"
	err := os.WriteFile(checksumFile, []byte(expectedChecksum), 0644)
	if err != nil {
		t.Fatalf("Failed to create checksum file: %v", err)
	}
	
	checksum, err := readChecksum(checksumFile)
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}
	
	if string(checksum) != expectedChecksum {
		t.Errorf("Expected checksum '%s', got '%s'", expectedChecksum, string(checksum))
	}
}

func TestReadChecksum_FileNotExists(t *testing.T) {
	_, err := readChecksum("/nonexistent/file.md5")
	if err == nil {
		t.Error("Expected error for non-existent file")
	}
}

func TestWriteChecksum_Success(t *testing.T) {
	tmpDir := t.TempDir()
	testFile := filepath.Join(tmpDir, "test.txt")
	checksumFile := testFile + ".md5"
	
	checksum := "abc123def456"
	mock := &mockChecksumProvider{checksum: checksum}
	
	err := WriteChecksum(mock, testFile)
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}
	
	// Verify checksum file was created with correct content
	content, err := os.ReadFile(checksumFile)
	if err != nil {
		t.Fatalf("Failed to read checksum file: %v", err)
	}
	
	if string(content) != checksum {
		t.Errorf("Expected checksum content '%s', got '%s'", checksum, string(content))
	}
}

func TestWriteChecksum_ProviderError(t *testing.T) {
	tmpDir := t.TempDir()
	testFile := filepath.Join(tmpDir, "test.txt")
	
	mock := &mockChecksumProvider{shouldError: true}
	
	err := WriteChecksum(mock, testFile)
	if err == nil {
		t.Error("Expected error when checksum provider fails")
	}
}

func TestWriteChecksum_WriteError(t *testing.T) {
	// Try to write to an invalid path (should fail)
	mock := &mockChecksumProvider{checksum: "abc123"}
	
	err := WriteChecksum(mock, "/invalid/path/file.txt")
	if err == nil {
		t.Error("Expected error when writing to invalid path")
	}
}