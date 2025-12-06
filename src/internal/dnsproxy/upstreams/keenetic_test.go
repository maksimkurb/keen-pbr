package upstreams

import (
	"testing"

	"github.com/maksimkurb/keen-pbr/src/internal/keenetic"
)

// mockKeeneticClient is a mock implementation of core.KeeneticClient for testing.
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

func TestKeeneticProvider_NewKeeneticProvider(t *testing.T) {
	client := &mockKeeneticClient{}

	// Test provider creation
	provider := NewKeeneticProvider(client, "")
	if provider.keeneticClient == nil {
		t.Error("Expected keeneticClient to be set")
	}

	// Domain parameter is ignored (Keenetic RCI provides domain per server)
	provider = NewKeeneticProvider(client, "example.com")
	if provider.GetDomain() != "" {
		t.Errorf("Expected empty domain (provider doesn't filter), got %q", provider.GetDomain())
	}
}

func TestKeeneticProvider_String(t *testing.T) {
	client := &mockKeeneticClient{}

	// Provider string is always "keenetic://" regardless of domain parameter
	provider := NewKeeneticProvider(client, "")
	if provider.String() != "keenetic://" {
		t.Errorf("Expected 'keenetic://', got %q", provider.String())
	}

	// Domain parameter is ignored
	provider = NewKeeneticProvider(client, "example.com")
	if provider.String() != "keenetic://" {
		t.Errorf("Expected 'keenetic://', got %q", provider.String())
	}
}

func TestKeeneticProvider_GetDNSServers(t *testing.T) {
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
	provider := NewKeeneticProvider(client, "")

	result := provider.GetDNSServers()
	if len(result) != 2 {
		t.Errorf("Expected 2 DNS servers, got %d", len(result))
	}
}

func TestKeeneticProvider_GetDNSServers_PassesThroughAllServers(t *testing.T) {
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

	// Provider doesn't filter - it returns all servers from Keenetic RCI
	provider := NewKeeneticProvider(client, "")
	result := provider.GetDNSServers()
	if len(result) != 2 {
		t.Errorf("Expected 2 DNS servers (all from RCI), got %d", len(result))
	}

	// Domain parameter is ignored - still returns all servers
	provider2 := NewKeeneticProvider(client, "specific.com")
	result2 := provider2.GetDNSServers()
	if len(result2) != 2 {
		t.Errorf("Expected 2 DNS servers (all from RCI), got %d", len(result2))
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

func TestKeeneticProvider_Close(t *testing.T) {
	servers := []keenetic.DNSServerInfo{
		{
			Type:     keenetic.DNSServerTypePlain,
			Proxy:    "8.8.8.8",
			Endpoint: "8.8.8.8",
		},
	}

	client := &mockKeeneticClient{dnsServers: servers}
	provider := NewKeeneticProvider(client, "")

	// Close should not panic (provider is stateless, so this is a no-op)
	err := provider.Close()
	if err != nil {
		t.Errorf("Expected no error on close, got %v", err)
	}
}
