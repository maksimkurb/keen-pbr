package dnsproxy

import (
	"testing"
	"time"
)

func TestIsDNSCheckDomain(t *testing.T) {
	tests := []struct {
		domain   string
		expected bool
	}{
		// Should match
		{"dns-check.keen-pbr.internal", true},
		{"DNS-CHECK.KEEN-PBR.INTERNAL", true},
		{"test.dns-check.keen-pbr.internal", true},
		{"TEST.DNS-CHECK.KEEN-PBR.INTERNAL", true},
		{"deep.subdomain.dns-check.keen-pbr.internal", true},
		{"123.dns-check.keen-pbr.internal", true},

		// Should not match
		{"example.com", false},
		{"keen-pbr.internal", false},
		{"other.keen-pbr.internal", false},
		{"dns-check.example.com", false},
		{"notdns-check.keen-pbr.internal", false},
		{"", false},
	}

	for _, tt := range tests {
		t.Run(tt.domain, func(t *testing.T) {
			result := isDNSCheckDomain(tt.domain)
			if result != tt.expected {
				t.Errorf("isDNSCheckDomain(%q) = %v, want %v", tt.domain, result, tt.expected)
			}
		})
	}
}

func TestDNSProxy_SubscribeBroadcast(t *testing.T) {
	// Create a minimal proxy for testing SSE functionality
	proxy := &DNSProxy{
		sseSubscribers: make(map[chan string]struct{}),
	}

	// Subscribe two clients
	ch1 := proxy.Subscribe()
	ch2 := proxy.Subscribe()

	// Broadcast a domain
	testDomain := "test.dns-check.keen-pbr.internal"
	proxy.broadcastDNSCheck(testDomain)

	// Both channels should receive the domain
	select {
	case domain := <-ch1:
		if domain != testDomain {
			t.Errorf("ch1 received %q, want %q", domain, testDomain)
		}
	case <-time.After(100 * time.Millisecond):
		t.Error("ch1 did not receive broadcast in time")
	}

	select {
	case domain := <-ch2:
		if domain != testDomain {
			t.Errorf("ch2 received %q, want %q", domain, testDomain)
		}
	case <-time.After(100 * time.Millisecond):
		t.Error("ch2 did not receive broadcast in time")
	}

	// Unsubscribe ch1
	proxy.Unsubscribe(ch1)

	// Broadcast again - only ch2 should receive
	testDomain2 := "test2.dns-check.keen-pbr.internal"
	proxy.broadcastDNSCheck(testDomain2)

	select {
	case domain := <-ch2:
		if domain != testDomain2 {
			t.Errorf("ch2 received %q, want %q", domain, testDomain2)
		}
	case <-time.After(100 * time.Millisecond):
		t.Error("ch2 did not receive second broadcast in time")
	}

	// Unsubscribe ch2
	proxy.Unsubscribe(ch2)

	// Verify both channels are closed
	_, ok := <-ch1
	if ok {
		t.Error("ch1 should be closed")
	}

	_, ok = <-ch2
	if ok {
		t.Error("ch2 should be closed")
	}
}

func TestDNSProxy_BroadcastFullChannel(t *testing.T) {
	// Create a minimal proxy for testing
	proxy := &DNSProxy{
		sseSubscribers: make(map[chan string]struct{}),
	}

	// Subscribe a client
	ch := proxy.Subscribe()

	// Fill up the channel buffer (buffer size is 10)
	for i := 0; i < 10; i++ {
		proxy.broadcastDNSCheck("test.dns-check.keen-pbr.internal")
	}

	// This should not block even though channel is full
	done := make(chan bool)
	go func() {
		proxy.broadcastDNSCheck("overflow.dns-check.keen-pbr.internal")
		done <- true
	}()

	select {
	case <-done:
		// Success - broadcast didn't block
	case <-time.After(100 * time.Millisecond):
		t.Error("broadcastDNSCheck blocked on full channel")
	}

	proxy.Unsubscribe(ch)
}
