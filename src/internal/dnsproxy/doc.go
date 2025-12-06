// Package dnsproxy provides a transparent DNS proxy that intercepts DNS traffic
// using iptables REDIRECT, processes DNS responses, and dynamically adds resolved
// IPs to ipsets with TTL-based expiration.
//
// The proxy supports multiple upstream resolver types:
//   - keenetic:// - fetches DNS servers from Keenetic RCI system profile
//   - udp://ip:port - plain UDP DNS resolver
//   - doh://host/path - DNS-over-HTTPS resolver
//
// Key features:
//   - Transparent interception of DNS traffic on port 53
//   - Domain matching against configured lists
//   - Automatic ipset population with TTL from DNS responses
//   - CNAME alias tracking for proper domain resolution
//   - Per-ipset DNS override support
//   - Optional AAAA record filtering (IPv6)
//
// The proxy integrates with the existing DomainStore for domain matching
// and IPSetManager for ipset operations.
package dnsproxy
