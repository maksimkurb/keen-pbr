package upstreams

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
)

// mockKeeneticClient is a mock implementation of domain.KeeneticClient for testing.
type mockKeeneticClient struct {
	dnsServers []keenetic.DNSServerInfo
	err        error
}

func (m *mockKeeneticClient) GetDNSServers() ([]keenetic.DNSServerInfo, error) {
	return m.dnsServers, m.err
}

func (m *mockKeeneticClient) GetInterfaces() (map[string]keenetic.Interface, error) {
	return nil, nil
}

func (m *mockKeeneticClient) GetVersion() (*keenetic.KeeneticVersion, error) {
	return nil, nil
}

func (m *mockKeeneticClient) GetRawVersion() (string, error) {
	return "", nil
}

func TestKeeneticUpstream_NewKeeneticUpstream(t *testing.T) {
	client := &mockKeeneticClient{}

	// Test without domain restriction
	upstream := NewKeeneticUpstream(client, "")
	if upstream.Domain != "" {
		t.Errorf("Expected empty domain, got %q", upstream.Domain)
	}
	if upstream.keeneticClient == nil {
		t.Error("Expected keeneticClient to be set")
	}

	// Test with domain restriction
	upstream = NewKeeneticUpstream(client, "example.com")
	if upstream.Domain != "example.com" {
		t.Errorf("Expected domain 'example.com', got %q", upstream.Domain)
	}
}

func TestKeeneticUpstream_String(t *testing.T) {
	client := &mockKeeneticClient{}

	// Without domain
	upstream := NewKeeneticUpstream(client, "")
	if upstream.String() != "keenetic://" {
		t.Errorf("Expected 'keenetic://', got %q", upstream.String())
	}

	// With domain
	upstream = NewKeeneticUpstream(client, "example.com")
	expected := "keenetic:// (domain: example.com)"
	if upstream.String() != expected {
		t.Errorf("Expected %q, got %q", expected, upstream.String())
	}
}

func TestKeeneticUpstream_GetDNSServers(t *testing.T) {
	servers := []keenetic.DNSServerInfo{
		{
			Type:     keenetic.DNSServerTypePlain,
			Proxy:    "8.8.8.8",
			Endpoint: "8.8.8.8",
		},
		{
			Type:     keenetic.DNSServerTypePlain,
			Proxy:    "8.8.4.4",
			Endpoint: "8.8.4.4",
		},
	}

	client := &mockKeeneticClient{dnsServers: servers}
	upstream := NewKeeneticUpstream(client, "")

	result := upstream.GetDNSServers()
	if len(result) != 2 {
		t.Errorf("Expected 2 DNS servers, got %d", len(result))
	}
}

func TestKeeneticUpstream_GetDNSServers_FiltersDomainSpecificServers(t *testing.T) {
	domain := "specific.com"
	servers := []keenetic.DNSServerInfo{
		{
			Type:     keenetic.DNSServerTypePlain,
			Proxy:    "8.8.8.8",
			Endpoint: "8.8.8.8",
			Domain:   nil, // General server
		},
		{
			Type:     keenetic.DNSServerTypePlain,
			Proxy:    "1.1.1.1",
			Endpoint: "1.1.1.1",
			Domain:   &domain, // Domain-specific server
		},
	}

	client := &mockKeeneticClient{dnsServers: servers}

	// Without domain restriction - should filter out domain-specific servers
	upstream := NewKeeneticUpstream(client, "")
	result := upstream.GetDNSServers()
	if len(result) != 1 {
		t.Errorf("Expected 1 DNS server (general only), got %d", len(result))
	}
	if result[0].Proxy != "8.8.8.8" {
		t.Errorf("Expected general server 8.8.8.8, got %s", result[0].Proxy)
	}

	// With domain restriction - should only include servers matching that domain
	upstream2 := NewKeeneticUpstream(client, "specific.com")
	result2 := upstream2.GetDNSServers()
	if len(result2) != 1 {
		t.Errorf("Expected 1 DNS server (domain-specific only), got %d", len(result2))
	}
	if len(result2) > 0 && result2[0].Proxy != "1.1.1.1" {
		t.Errorf("Expected domain-specific server 1.1.1.1, got %s", result2[0].Proxy)
	}
}

func TestCreateUpstreamFromDNSServerInfo_PlainIPv4(t *testing.T) {
	info := keenetic.DNSServerInfo{
		Type:     keenetic.DNSServerTypePlain,
		Proxy:    "8.8.8.8",
		Endpoint: "8.8.8.8",
	}

	upstream := createUpstreamFromDNSServerInfo(info)
	if upstream == nil {
		t.Fatal("Expected upstream to be created")
	}

	udp, ok := upstream.(*UDPUpstream)
	if !ok {
		t.Fatalf("Expected *UDPUpstream, got %T", upstream)
	}

	if udp.address != "8.8.8.8:53" {
		t.Errorf("Expected address '8.8.8.8:53', got %q", udp.address)
	}
}

func TestCreateUpstreamFromDNSServerInfo_PlainIPv4WithPort(t *testing.T) {
	info := keenetic.DNSServerInfo{
		Type:     keenetic.DNSServerTypePlain,
		Proxy:    "8.8.8.8",
		Endpoint: "8.8.8.8",
		Port:     "5353",
	}

	upstream := createUpstreamFromDNSServerInfo(info)
	if upstream == nil {
		t.Fatal("Expected upstream to be created")
	}

	udp, ok := upstream.(*UDPUpstream)
	if !ok {
		t.Fatalf("Expected *UDPUpstream, got %T", upstream)
	}

	if udp.address != "8.8.8.8:5353" {
		t.Errorf("Expected address '8.8.8.8:5353', got %q", udp.address)
	}
}

func TestCreateUpstreamFromDNSServerInfo_PlainIPv6(t *testing.T) {
	info := keenetic.DNSServerInfo{
		Type:     keenetic.DNSServerTypePlainIPv6,
		Proxy:    "2001:4860:4860::8888",
		Endpoint: "2001:4860:4860::8888",
	}

	upstream := createUpstreamFromDNSServerInfo(info)
	if upstream == nil {
		t.Fatal("Expected upstream to be created")
	}

	udp, ok := upstream.(*UDPUpstream)
	if !ok {
		t.Fatalf("Expected *UDPUpstream, got %T", upstream)
	}

	if udp.address != "[2001:4860:4860::8888]:53" {
		t.Errorf("Expected address '[2001:4860:4860::8888]:53', got %q", udp.address)
	}
}

func TestCreateUpstreamFromDNSServerInfo_DoT(t *testing.T) {
	info := keenetic.DNSServerInfo{
		Type:     keenetic.DNSServerTypeDoT,
		Proxy:    "127.0.0.1",
		Endpoint: "dns.google",
		Port:     "40500",
	}

	upstream := createUpstreamFromDNSServerInfo(info)
	if upstream == nil {
		t.Fatal("Expected upstream to be created")
	}

	// DoT is proxied via local UDP
	udp, ok := upstream.(*UDPUpstream)
	if !ok {
		t.Fatalf("Expected *UDPUpstream, got %T", upstream)
	}

	if udp.address != "127.0.0.1:40500" {
		t.Errorf("Expected address '127.0.0.1:40500', got %q", udp.address)
	}
}

func TestCreateUpstreamFromDNSServerInfo_DoH(t *testing.T) {
	info := keenetic.DNSServerInfo{
		Type:     keenetic.DNSServerTypeDoH,
		Proxy:    "127.0.0.1",
		Endpoint: "https://dns.google/dns-query",
		Port:     "40501",
	}

	upstream := createUpstreamFromDNSServerInfo(info)
	if upstream == nil {
		t.Fatal("Expected upstream to be created")
	}

	// DoH is proxied via local UDP
	udp, ok := upstream.(*UDPUpstream)
	if !ok {
		t.Fatalf("Expected *UDPUpstream, got %T", upstream)
	}

	if udp.address != "127.0.0.1:40501" {
		t.Errorf("Expected address '127.0.0.1:40501', got %q", udp.address)
	}
}

func TestCreateUpstreamFromDNSServerInfo_WithDomain(t *testing.T) {
	domain := "example.com"
	info := keenetic.DNSServerInfo{
		Type:     keenetic.DNSServerTypePlain,
		Proxy:    "8.8.8.8",
		Endpoint: "8.8.8.8",
		Domain:   &domain,
	}

	upstream := createUpstreamFromDNSServerInfo(info)
	if upstream == nil {
		t.Fatal("Expected upstream to be created")
	}

	if upstream.GetDomain() != "example.com" {
		t.Errorf("Expected domain 'example.com', got %q", upstream.GetDomain())
	}

	// Should match exact domain
	if !upstream.MatchesDomain("example.com") {
		t.Error("Expected to match 'example.com'")
	}

	// Should match subdomain
	if !upstream.MatchesDomain("sub.example.com") {
		t.Error("Expected to match 'sub.example.com'")
	}

	// Should not match different domain
	if upstream.MatchesDomain("other.com") {
		t.Error("Should not match 'other.com'")
	}
}

func TestCreateUpstreamFromDNSServerInfo_UnknownType(t *testing.T) {
	info := keenetic.DNSServerInfo{
		Type:     "unknown",
		Proxy:    "8.8.8.8",
		Endpoint: "8.8.8.8",
	}

	upstream := createUpstreamFromDNSServerInfo(info)
	if upstream != nil {
		t.Error("Expected nil for unknown type")
	}
}

func TestKeeneticUpstream_Close(t *testing.T) {
	servers := []keenetic.DNSServerInfo{
		{
			Type:     keenetic.DNSServerTypePlain,
			Proxy:    "8.8.8.8",
			Endpoint: "8.8.8.8",
		},
	}

	client := &mockKeeneticClient{dnsServers: servers}
	upstream := NewKeeneticUpstream(client, "")

	// Trigger upstream creation
	_ = upstream.GetDNSServers()

	// Close should not panic
	err := upstream.Close()
	if err != nil {
		t.Errorf("Expected no error on close, got %v", err)
	}

	// After close, cached servers should be nil
	upstream.mu.RLock()
	if upstream.cachedServers != nil {
		t.Error("Expected cachedServers to be nil after close")
	}
	if upstream.upstreams != nil {
		t.Error("Expected upstreams to be nil after close")
	}
	upstream.mu.RUnlock()
}

func TestKeeneticUpstream_CacheTTL(t *testing.T) {
	callCount := 0
	servers := []keenetic.DNSServerInfo{
		{
			Type:     keenetic.DNSServerTypePlain,
			Proxy:    "8.8.8.8",
			Endpoint: "8.8.8.8",
		},
	}

	client := &mockKeeneticClient{dnsServers: servers}

	// Wrap GetDNSServers to count calls
	originalClient := client
	countingClient := &countingMockClient{
		mockKeeneticClient: originalClient,
		callCount:          &callCount,
	}

	upstream := NewKeeneticUpstream(countingClient, "")

	// First call should fetch from client
	_ = upstream.GetDNSServers()
	if callCount != 1 {
		t.Errorf("Expected 1 call, got %d", callCount)
	}

	// Second call should use cache
	_ = upstream.GetDNSServers()
	if callCount != 1 {
		t.Errorf("Expected still 1 call (cached), got %d", callCount)
	}
}

// countingMockClient wraps mockKeeneticClient to count GetDNSServers calls.
type countingMockClient struct {
	*mockKeeneticClient
	callCount *int
}

func (c *countingMockClient) GetDNSServers() ([]keenetic.DNSServerInfo, error) {
	*c.callCount++
	return c.mockKeeneticClient.GetDNSServers()
}

func (c *countingMockClient) GetInterfaces() (map[string]keenetic.Interface, error) {
	return nil, nil
}

func (c *countingMockClient) GetVersion() (*keenetic.KeeneticVersion, error) {
	return nil, nil
}

func (c *countingMockClient) GetRawVersion() (string, error) {
	return "", nil
}
