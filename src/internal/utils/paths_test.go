package utils

import (
	"path/filepath"
	"runtime"
	"testing"
)

func TestGetAbsolutePath_AlreadyAbsolute(t *testing.T) {
	var absolutePath string
	if runtime.GOOS == "windows" {
		absolutePath = "C:\\test\\file.txt"
	} else {
		absolutePath = "/test/file.txt"
	}
	
	result := GetAbsolutePath(absolutePath, "/base/dir")
	
	if result != absolutePath {
		t.Errorf("Expected %s, got %s", absolutePath, result)
	}
}

func TestGetAbsolutePath_RelativePath(t *testing.T) {
	relativePath := "relative/file.txt"
	baseDir := "/base/dir"
	
	result := GetAbsolutePath(relativePath, baseDir)
	expected := "/base/dir/relative/file.txt"
	
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}

func TestGetAbsolutePath_DotPath(t *testing.T) {
	relativePath := "./file.txt"
	baseDir := "/base/dir"
	
	result := GetAbsolutePath(relativePath, baseDir)
	expected := "/base/dir/file.txt"
	
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}

func TestGetAbsolutePath_DoubleDotPath(t *testing.T) {
	relativePath := "../file.txt"
	baseDir := "/base/dir"
	
	result := GetAbsolutePath(relativePath, baseDir)
	expected := "/base/file.txt"
	
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}

func TestGetAbsolutePath_EmptyPath(t *testing.T) {
	relativePath := ""
	baseDir := "/base/dir"
	
	result := GetAbsolutePath(relativePath, baseDir)
	expected := "/base/dir"
	
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}

func TestGetAbsolutePath_EmptyBaseDir(t *testing.T) {
	relativePath := "file.txt"
	baseDir := ""
	
	result := GetAbsolutePath(relativePath, baseDir)
	expected := "file.txt"
	
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}

func TestGetAbsolutePath_ComplexRelativePath(t *testing.T) {
	relativePath := "a/b/../c/./d/file.txt"
	baseDir := "/base/dir"
	
	result := GetAbsolutePath(relativePath, baseDir)
	expected := "/base/dir/a/c/d/file.txt"
	
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}

func TestGetAbsolutePath_PathCleaning(t *testing.T) {
	// Test that the function properly cleans the resulting path
	relativePath := "a//b///c/file.txt"
	baseDir := "/base//dir"
	
	result := GetAbsolutePath(relativePath, baseDir)
	expected := "/base/dir/a/b/c/file.txt"
	
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}

func TestGetAbsolutePath_WithFilepathSeparator(t *testing.T) {
	// Test using platform-specific separators
	relativePath := filepath.Join("subdir", "file.txt")
	baseDir := filepath.Join("/", "base", "dir")
	
	result := GetAbsolutePath(relativePath, baseDir)
	expected := filepath.Join("/", "base", "dir", "subdir", "file.txt")
	
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}

func TestGetAbsolutePath_EdgeCases(t *testing.T) {
	result := GetAbsolutePath("../../file.txt", "/base/sub1/sub2")
	expected := "/base/file.txt"
	if result != expected {
		t.Errorf("Expected %s, got %s", expected, result)
	}
}