---
title: Concepts
weight: 2
---

You do not need this page for a normal setup. It explains how keen-pbr decides where traffic goes.

## The idea: lists, rules, outbounds

The easiest comparison is a post office sorting letters.

- A **list** is an address book. One book holds the sites you want to open through a VPN, another holds addresses to block.
- A **routing rule** is a sorting instruction: "send letters from this book along this route". Instructions are read from top to bottom, and the first one that fits wins.
- An **outbound** is a delivery route: a VPN interface, your normal provider connection, or "throw the letter away".

A letter whose address is in no book goes the usual way.

For example, you have two books, `streaming` (streaming services) and `work` (work sites). The instructions are:

1. Addresses from `local_networks` (your home network) are not sent anywhere and stay inside the network.
2. Addresses from `work` go through the work VPN.
3. Addresses from `streaming` go through a second VPN.

Everything else goes directly through your provider.

## Lists

A list is a set of domains, IP addresses and subnets (CIDR ranges such as `192.168.0.0/16`). It can be built from several sources:

- **URL**: the list is downloaded from the internet and refreshed on a schedule;
- **File on the device**: the list is read from a file on the router;
- **Domains / IPs**: you write the addresses directly in the config.

For domains you do not need to know IP addresses. keen-pbr learns the addresses of listed sites when devices on your network open them, and keeps them for a limited time. After you change lists, clear the DNS cache on your device: otherwise it may connect to a site by an old address that keen-pbr does not know yet.

See [Lists]({{< relref "/docs/configuration/lists" >}}).

## Outbounds

An outbound is a channel traffic goes through. There are six types:

| Type | What it does |
|---|---|
| `interface` | Sends traffic through the chosen network interface, for example a VPN. You can also set IPv4 and IPv6 gateways |
| `table` | Sends traffic along one of the router's routing tables. Table 254 is the normal internet path through your provider |
| `blackhole` | Drops the traffic, that is, blocks it |
| `ignore` | Leaves the traffic as is: it goes the usual way and the rules below it are not checked |
| `urltest` | Picks the best of several channels: checks them with an HTTP request and takes the one with the lowest latency or spreads connections between them. An unstable channel is temporarily excluded |
| `icmptest` | Same as `urltest`, but channels are checked with ping ({{< term "ICMP" >}}) |

`ignore` is for exceptions. For example, a "home network → `ignore`" rule is placed above the general VPN rule so local traffic does not enter the tunnel.

See [Outbounds]({{< relref "/docs/configuration/outbounds" >}}).

## Routing rules

A rule links conditions to an outbound. Conditions can be combined:

- **Lists**: the address is in one of the given lists;
- **Protocol**: `tcp` or `udp`;
- **DSCP**: a traffic priority tag;
- **Source IP** and **Destination IP**: an address or subnet ({{< term "CIDR" >}}), optionally negated;
- **Source port** and **Destination port**: a single port, a list or a range.

If a rule has several conditions, traffic must match all of them. Rules are checked from top to bottom, and the first matching rule wins. Traffic that matches no rule goes the usual way.

See [Routing rules]({{< relref "/docs/configuration/route-rules" >}}).

## DNS

Domain-based routing needs no DNS server setup. dnsmasq, the Keenetic DNS proxy or any other resolver keeps working as before. keen-pbr learns the addresses of listed sites from DNS replies and from site names in connections that pass through the router.

If some lists need different DNS servers, for example to get CDN addresses for the right region, use [per-list DNS servers]({{< relref "/docs/configuration/dns#per-list-dns-servers-dnsmasq" >}}) (via dnsmasq).

See [DNS]({{< relref "/docs/configuration/dns" >}}).

## fwmark and conntrack {#fwmark-and-conntrack}

You will meet these two words in logs and search results when something goes wrong. You normally do not need to configure them.

- **{{< term "fwmark" >}}** is a numeric tag on a packet. keen-pbr puts it on traffic from your lists, and the tag tells the router which outbound to send the packet to. Each outbound has its own tag from the range set in the [`fwmark`]({{< relref "/docs/configuration/advanced#fwmark" >}}) setting.
- **{{< term "conntrack" >}}** is the router's table of open connections. It keeps all packets of one connection on the same path. When a connection has to move to another channel, keen-pbr clears its entry in this table and the device opens the connection again.
