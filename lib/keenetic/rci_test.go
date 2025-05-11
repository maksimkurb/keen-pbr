package keenetic

import (
	"reflect"
	"testing"
)

func strPtr(s string) *string { return &s }

func TestParseDnsProxyConfig_DoT(t *testing.T) {
	const testConfig = `rpc_port = 54321
rpc_ttl = 10000
rpc_wait = 10000
timeout = 7000
proceed = 500
stat_file = /var/ndnproxymain.stat
stat_time = 10000
rr_port = 40901
dns_server = 192.168.41.15 cubly.ru
dns_server = 127.0.0.1:40500 . # p0.freedns.controld.com
static_a = my.keenetic.net 78.47.125.180
static_a = a3fd26f19802c3c1101c2d0d.keenetic.io 78.47.125.180
static_aaaa = a3fd26f19802c3c1101c2d0d.keenetic.io ::
norebind_ctl = off
set-profile-ip 127.0.0.1 0
set-profile-ip ::1 0
rpc_only = on
`

	got := ParseDnsProxyConfig(testConfig)
	want := []DnsServerInfo{
		{
			Type:     DnsServerTypePlain,
			Domain:   strPtr("cubly.ru"),
			Proxy:    "192.168.41.15",
			Endpoint: "192.168.41.15",
			Port:     "",
		},
		{
			Type:     DnsServerTypeDoT,
			Domain:   nil,
			Proxy:    "127.0.0.1:40500",
			Endpoint: "p0.freedns.controld.com",
			Port:     "",
		},
	}

	if !reflect.DeepEqual(got, want) {
		t.Errorf("ParseDnsProxyConfig() = %#v, want %#v", got, want)
	}
}

func TestParseDnsProxyConfig_DoH(t *testing.T) {
	const testConfig = `rpc_port = 54321
rpc_ttl = 10000
rpc_wait = 10000
timeout = 7000
proceed = 500
stat_file = /var/ndnproxymain.stat
stat_time = 10000
rr_port = 40901
dns_server = 127.0.0.1:40508 . # https://freedns.controld.com/p0@dnsm
static_a = my.keenetic.net 78.47.125.180
static_a = a4e7905b43148e589ce3223c.keenetic.io 78.47.125.180
static_aaaa = a4e7905b43148e589ce3223c.keenetic.io ::
norebind_ctl = on
norebind_ip4net = 10.1.30.1:24
norebind_ip4net = 192.168.54.1:24
norebind_ip4net = 255.255.255.255:32
set-profile-ip 127.0.0.1 0
set-profile-ip ::1 0
rpc_only = on
mirror_path = /var/run/ntce-dns-mirror.sock
`

	got := ParseDnsProxyConfig(testConfig)
	want := []DnsServerInfo{
		{
			Type:     DnsServerTypeDoH,
			Domain:   nil,
			Proxy:    "127.0.0.1",
			Endpoint: "https://freedns.controld.com/p0",
			Port:     "40508",
		},
	}

	if !reflect.DeepEqual(got, want) {
		t.Errorf("ParseDnsProxyConfig() = %#v, want %#v", got, want)
	}
}

func TestParseDnsProxyConfig_DoH2(t *testing.T) {
	const testConfig = `rpc_port = 54321
rpc_ttl = 10000
rpc_wait = 10000
timeout = 7000
proceed = 500
stat_file = /var/ndnproxymain.stat
stat_time = 10000
rr_port = 40901
dns_server = 127.0.0.1:40508 . # https://freedns.controld.com/p0
static_a = my.keenetic.net 78.47.125.180
static_a = a4e7905b43148e589ce3223c.keenetic.io 78.47.125.180
static_aaaa = a4e7905b43148e589ce3223c.keenetic.io ::
norebind_ctl = on
norebind_ip4net = 10.1.30.1:24
norebind_ip4net = 192.168.54.1:24
norebind_ip4net = 255.255.255.255:32
set-profile-ip 127.0.0.1 0
set-profile-ip ::1 0
rpc_only = on
mirror_path = /var/run/ntce-dns-mirror.sock
`

	got := ParseDnsProxyConfig(testConfig)
	want := []DnsServerInfo{
		{
			Type:     DnsServerTypeDoH,
			Domain:   nil,
			Proxy:    "127.0.0.1",
			Endpoint: "https://freedns.controld.com/p0",
			Port:     "40508",
		},
	}

	if !reflect.DeepEqual(got, want) {
		t.Errorf("ParseDnsProxyConfig() = %#v, want %#v", got, want)
	}
}

func TestParseDnsProxyConfig_Complex(t *testing.T) {
	const testConfig = `rpc_port = 54321
rpc_ttl = 10000
rpc_wait = 10000
timeout = 7000
proceed = 500
stat_file = /var/ndnproxymain.stat
stat_time = 10000
rr_port = 40901
dns_server = 2606:1a40::3 ipv6.corp
dns_server = 1.2.2.1 ipv4.corp
dns_server = 121.11.1.1:124 corp.me
dns_server = 127.0.0.1:40500 . # p1.freedns.controld.com
dns_server = 127.0.0.1:40501 . # 76.76.2.11@p0.freedns.controld.com
dns_server = 127.0.0.1:40508 . # https://freedns.controld.com/p0@dnsm
static_a = my.keenetic.net 78.47.125.180
static_a = a4e7905b43148e589ce3223c.keenetic.io 78.47.125.180
static_aaaa = a4e7905b43148e589ce3223c.keenetic.io ::
norebind_ctl = on
norebind_ip4net = 10.1.30.1:24
norebind_ip4net = 192.168.54.1:24
norebind_ip4net = 255.255.255.255:32
norebind_exclude ipv6.corp
norebind_exclude *.ipv6.corp
norebind_exclude ipv4.corp
norebind_exclude *.ipv4.corp
set-profile-ip 127.0.0.1 0
set-profile-ip ::1 0
rpc_only = on
mirror_path = /var/run/ntce-dns-mirror.sock
`

	got := ParseDnsProxyConfig(testConfig)
	want := []DnsServerInfo{
		{
			Type:     DnsServerTypePlainIPv6,
			Domain:   strPtr("ipv6.corp"),
			Proxy:    "2606:1a40::3",
			Endpoint: "2606:1a40::3",
			Port:     "",
		},
		{
			Type:     DnsServerTypePlain,
			Domain:   strPtr("ipv4.corp"),
			Proxy:    "1.2.2.1",
			Endpoint: "1.2.2.1",
			Port:     "",
		},
		{
			Type:     DnsServerTypePlain,
			Domain:   strPtr("corp.me"),
			Proxy:    "121.11.1.1",
			Endpoint: "121.11.1.1",
			Port:     "124",
		},
		{
			Type:     DnsServerTypeDoT,
			Domain:   nil,
			Proxy:    "127.0.0.1:40500",
			Endpoint: "p1.freedns.controld.com",
			Port:     "",
		},
		{
			Type:     DnsServerTypeDoT,
			Domain:   nil,
			Proxy:    "127.0.0.1:40501",
			Endpoint: "76.76.2.11@p0.freedns.controld.com",
			Port:     "",
		},
		{
			Type:     DnsServerTypeDoH,
			Domain:   nil,
			Proxy:    "127.0.0.1",
			Endpoint: "https://freedns.controld.com/p0",
			Port:     "40508",
		},
	}

	if !reflect.DeepEqual(got, want) {
		t.Errorf("ParseDnsProxyConfig() = %#v, want %#v", got, want)
	}
}
