package utils

import (
	"io"
	"strings"
	"testing"
)

// Mock closer for testing
type mockCloser struct {
	shouldError bool
	closed      bool
}

func (m *mockCloser) Close() error {
	m.closed = true
	if m.shouldError {
		return io.ErrUnexpectedEOF
	}
	return nil
}

func TestCloseOrPanic_Success(t *testing.T) {
	mock := &mockCloser{shouldError: false}
	
	// Should not panic
	CloseOrPanic(mock)
	
	if !mock.closed {
		t.Error("Expected Close to be called")
	}
}

func TestCloseOrPanic_Panic(t *testing.T) {
	mock := &mockCloser{shouldError: true}
	
	defer func() {
		r := recover()
		if r == nil {
			t.Error("Expected panic but didn't get one")
		}
		
		if !mock.closed {
			t.Error("Expected Close to be called even on error")
		}
		
		// Verify the panic value is the expected error
		if r != io.ErrUnexpectedEOF {
			t.Errorf("Expected panic with ErrUnexpectedEOF, got %v", r)
		}
	}()
	
	CloseOrPanic(mock)
}

func TestCloseOrPanic_WithRealCloser(t *testing.T) {
	// Test with a real io.Closer
	reader := strings.NewReader("test content")
	closer := io.NopCloser(reader)
	
	// Should not panic
	CloseOrPanic(closer)
	
	// Verify it's closed by trying to close again (should still work)
	err := closer.Close()
	if err != nil {
		t.Errorf("Unexpected error on second close: %v", err)
	}
}