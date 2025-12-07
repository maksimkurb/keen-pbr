package dnsproxy

import (
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/core"
)

// DNSProxy is a facade that combines the Server (network layer) and ProxyHandler (business logic).
type DNSProxy struct {
	server  *Server
	handler *ProxyHandler
}

// NewDNSProxy creates a new DNS proxy with the refactored architecture.
func NewDNSProxy(
	cfg ProxyConfig,
	keeneticClient core.KeeneticClient,
	ipsetManager core.IPSetManager,
	appConfig *config.Config,
) (*DNSProxy, error) {
	handler, err := NewProxyHandler(cfg, keeneticClient, ipsetManager, appConfig)
	if err != nil {
		return nil, err
	}

	return &DNSProxy{
		server:  NewServer(cfg, handler),
		handler: handler,
	}, nil
}

// Start starts the DNS proxy (both server and handler).
func (p *DNSProxy) Start() error {
	return p.server.Start()
}

// Stop stops the DNS proxy (both server and handler).
func (p *DNSProxy) Stop() error {
	if err := p.server.Stop(); err != nil {
		return err
	}
	p.handler.Shutdown()
	return nil
}

// ReloadLists reloads domain lists in the handler.
func (p *DNSProxy) ReloadLists() {
	p.handler.ReloadLists()
}

// MatchesIPSets returns the ipset names that match the given domain.
func (p *DNSProxy) MatchesIPSets(domain string) []string {
	return p.handler.MatchesIPSets(domain)
}

// GetDNSStrings returns the list of DNS server strings currently used.
func (p *DNSProxy) GetDNSStrings() []string {
	return p.handler.GetDNSStrings()
}

// Subscribe adds a new SSE subscriber for DNS check events.
func (p *DNSProxy) Subscribe() chan string {
	return p.handler.Subscribe()
}

// Unsubscribe removes an SSE subscriber.
func (p *DNSProxy) Unsubscribe(ch chan string) {
	p.handler.Unsubscribe(ch)
}

// CloseAllSubscribers closes all SSE subscriber channels.
func (p *DNSProxy) CloseAllSubscribers() {
	p.handler.CloseAllSubscribers()
}
