// Package dnscheck provides DNS query interception and broadcasting functionality
// for split-DNS configuration verification.
//
// The package implements a UDP DNS server that listens for DNS queries to
// *.dns-check.keen-pbr.internal domains, broadcasts them via Server-Sent Events (SSE),
// and responds with a static IP address (192.168.255.255).
//
// This allows the web UI to verify that DNS queries from the browser are being
// properly routed through the router's DNS server (dnsmasq) and reaching keen-pbr.
package dnscheck
