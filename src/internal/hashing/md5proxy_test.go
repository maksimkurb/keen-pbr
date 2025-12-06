package hashing

import (
	"crypto/md5"
	"encoding/hex"
	"errors"
	"io"
	"strings"
	"testing"
)

type errorReader struct {
	err error
}

func (e *errorReader) Read(p []byte) (n int, err error) {
	return 0, e.err
}

func TestNewMD5ReaderProxy(t *testing.T) {
	reader := strings.NewReader("test data")
	proxy := NewMD5ReaderProxy(reader)
	
	if proxy == nil {
		t.Fatal("Expected proxy to be non-nil")
	}
	
	if proxy.reader != reader {
		t.Error("Expected reader to be set correctly")
	}
	
	if proxy.checksum == nil {
		t.Error("Expected checksum to be initialized")
	}
}

func TestChecksumReaderProxy_Read(t *testing.T) {
	testData := "hello world"
	reader := strings.NewReader(testData)
	proxy := NewMD5ReaderProxy(reader)
	
	buf := make([]byte, 5)
	n, err := proxy.Read(buf)
	
	if err != nil {
		t.Errorf("Unexpected error: %v", err)
	}
	
	if n != 5 {
		t.Errorf("Expected to read 5 bytes, got %d", n)
	}
	
	if string(buf) != "hello" {
		t.Errorf("Expected 'hello', got '%s'", string(buf))
	}
}

func TestChecksumReaderProxy_ReadAll(t *testing.T) {
	testData := "hello world"
	reader := strings.NewReader(testData)
	proxy := NewMD5ReaderProxy(reader)
	
	allData, err := io.ReadAll(proxy)
	if err != nil {
		t.Errorf("Unexpected error: %v", err)
	}
	
	if string(allData) != testData {
		t.Errorf("Expected '%s', got '%s'", testData, string(allData))
	}
}

func TestChecksumReaderProxy_ReadError(t *testing.T) {
	expectedErr := errors.New("read error")
	reader := &errorReader{err: expectedErr}
	proxy := NewMD5ReaderProxy(reader)
	
	buf := make([]byte, 10)
	_, err := proxy.Read(buf)
	
	if err != expectedErr {
		t.Errorf("Expected error %v, got %v", expectedErr, err)
	}
}

func TestChecksumReaderProxy_GetChecksum(t *testing.T) {
	testData := "hello world"
	reader := strings.NewReader(testData)
	proxy := NewMD5ReaderProxy(reader)
	
	// Read all data
	_, err := io.ReadAll(proxy)
	if err != nil {
		t.Fatalf("Failed to read data: %v", err)
	}
	
	checksum, err := proxy.GetChecksum()
	if err != nil {
		t.Errorf("Unexpected error getting checksum: %v", err)
	}
	
	// Calculate expected checksum
	hasher := md5.New()
	hasher.Write([]byte(testData))
	expected := hex.EncodeToString(hasher.Sum(nil))
	
	if checksum != expected {
		t.Errorf("Expected checksum %s, got %s", expected, checksum)
	}
}

func TestChecksumReaderProxy_GetChecksumEmpty(t *testing.T) {
	reader := strings.NewReader("")
	proxy := NewMD5ReaderProxy(reader)
	
	// Read all data (empty)
	_, err := io.ReadAll(proxy)
	if err != nil {
		t.Fatalf("Failed to read data: %v", err)
	}
	
	checksum, err := proxy.GetChecksum()
	if err != nil {
		t.Errorf("Unexpected error getting checksum: %v", err)
	}
	
	// MD5 of empty string
	expected := "d41d8cd98f00b204e9800998ecf8427e"
	
	if checksum != expected {
		t.Errorf("Expected checksum %s, got %s", expected, checksum)
	}
}


