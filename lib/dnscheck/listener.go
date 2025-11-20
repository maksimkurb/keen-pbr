package dnscheck

import (
	"context"
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/maksimkurb/keen-pbr/lib/log"
)

// DNSCheckListener listens for DNS queries and broadcasts them to SSE clients
type DNSCheckListener struct {
	port int
	conn *net.UDPConn
	ctx  context.Context
	cancel context.CancelFunc

	// SSE broadcasting
	mu          sync.RWMutex
	subscribers map[chan string]struct{}
}

// NewDNSCheckListener creates a new DNS check listener
func NewDNSCheckListener(port int) *DNSCheckListener {
	ctx, cancel := context.WithCancel(context.Background())
	return &DNSCheckListener{
		port:        port,
		ctx:         ctx,
		cancel:      cancel,
		subscribers: make(map[chan string]struct{}),
	}
}

// Start starts the UDP listener
func (l *DNSCheckListener) Start() error {
	addr := net.UDPAddr{
		Port: l.port,
		IP:   net.ParseIP("127.0.50.50"),
	}

	conn, err := net.ListenUDP("udp", &addr)
	if err != nil {
		return fmt.Errorf("failed to start DNS check listener: %v", err)
	}
	l.conn = conn

	log.Infof("DNS check listener started on 127.0.50.50:%d", l.port)

	go l.listen()
	return nil
}

// listen handles incoming DNS queries
func (l *DNSCheckListener) listen() {
	buffer := make([]byte, 512) // Standard DNS packet size

	for {
		select {
		case <-l.ctx.Done():
			return
		default:
		}

		l.conn.SetReadDeadline(time.Now().Add(1 * time.Second))
		n, addr, err := l.conn.ReadFromUDP(buffer)
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			log.Debugf("Error reading DNS query: %v", err)
			continue
		}

		// Parse DNS query to extract domain name
		domain := l.parseDNSQuery(buffer[:n])
		if domain != "" {
			log.Debugf("DNS check query received: %s from %s", domain, addr)

			// Broadcast to SSE subscribers
			l.broadcast(domain)

			// Send DNS response with IP 192.168.255.255
			l.sendDNSResponse(buffer[:n], addr)
		}
	}
}

// parseDNSQuery extracts the domain name from a DNS query packet
func (l *DNSCheckListener) parseDNSQuery(packet []byte) string {
	// DNS query format:
	// Header: 12 bytes
	// Question: variable length
	// QNAME format: labels with length prefixes, ending with 0

	if len(packet) < 12 {
		return ""
	}

	// Skip DNS header (12 bytes)
	pos := 12
	domain := ""

	for pos < len(packet) {
		labelLen := int(packet[pos])
		if labelLen == 0 {
			break
		}

		// Check for DNS compression pointer (not expected in queries, but handle it)
		if labelLen >= 192 { // 0xC0
			break
		}

		pos++
		if pos+labelLen > len(packet) {
			return ""
		}

		if domain != "" {
			domain += "."
		}
		domain += string(packet[pos : pos+labelLen])
		pos += labelLen
	}

	return domain
}

// sendDNSResponse sends a DNS response with IP 192.168.255.255
func (l *DNSCheckListener) sendDNSResponse(query []byte, addr *net.UDPAddr) {
	if len(query) < 12 {
		return
	}

	// Build DNS response
	response := make([]byte, len(query)+16) // query + answer section

	// Copy query
	copy(response, query)

	// Set response flags (QR=1, AA=1, RCODE=0)
	response[2] = 0x84 // 10000100
	response[3] = 0x00

	// Set answer count to 1
	response[6] = 0x00
	response[7] = 0x01

	// Find end of question section
	pos := 12
	for pos < len(query) && query[pos] != 0 {
		labelLen := int(query[pos])
		if labelLen >= 192 { // Compression pointer
			pos += 2
			break
		}
		pos += 1 + labelLen
	}
	if pos < len(query) && query[pos] == 0 {
		pos++
	}
	pos += 4 // Skip QTYPE and QCLASS

	// Answer section (copy question name + response)
	answerStart := pos

	// Name (pointer to question)
	response[answerStart] = 0xc0
	response[answerStart+1] = 0x0c

	// Type A (0x0001)
	response[answerStart+2] = 0x00
	response[answerStart+3] = 0x01

	// Class IN (0x0001)
	response[answerStart+4] = 0x00
	response[answerStart+5] = 0x01

	// TTL (1 second)
	response[answerStart+6] = 0x00
	response[answerStart+7] = 0x00
	response[answerStart+8] = 0x00
	response[answerStart+9] = 0x01

	// RDLENGTH (4 bytes for IPv4)
	response[answerStart+10] = 0x00
	response[answerStart+11] = 0x04

	// RDATA (192.168.255.255)
	response[answerStart+12] = 192
	response[answerStart+13] = 168
	response[answerStart+14] = 255
	response[answerStart+15] = 255

	responseLen := answerStart + 16
	l.conn.WriteToUDP(response[:responseLen], addr)
}

// broadcast sends the domain to all SSE subscribers
func (l *DNSCheckListener) broadcast(domain string) {
	l.mu.RLock()
	defer l.mu.RUnlock()

	for ch := range l.subscribers {
		select {
		case ch <- domain:
		default:
			// Channel full, skip
		}
	}
}

// Subscribe adds a new SSE subscriber
func (l *DNSCheckListener) Subscribe() <-chan string {
	ch := make(chan string, 10)
	l.mu.Lock()
	l.subscribers[ch] = struct{}{}
	l.mu.Unlock()
	return ch
}

// Unsubscribe removes an SSE subscriber
func (l *DNSCheckListener) Unsubscribe(ch <-chan string) {
	l.mu.Lock()
	delete(l.subscribers, ch.(chan string))
	l.mu.Unlock()
	close(ch.(chan string))
}

// Stop stops the DNS check listener
func (l *DNSCheckListener) Stop() error {
	l.cancel()
	if l.conn != nil {
		return l.conn.Close()
	}
	return nil
}
