---
title: keen-pbr3
toc: false
---

{{< hextra/hero-badge >}}
  <div class="hx-w-2 hx-h-2 hx-rounded-full hx-bg-primary-400"></div>
  <span>Policy-based routing for Linux</span>
  {{< icon name="arrow-circle-right" attributes="height=14" >}}
{{< /hextra/hero-badge >}}

<div class="hx-mt-6 hx-mb-6">
{{< hextra/hero-headline >}}
  keen-pbr3
{{< /hextra/hero-headline >}}
</div>

<div class="hx-mb-12">
{{< hextra/hero-subtitle >}}
  Policy-based routing daemon for OpenWRT and Keenetic routers.&nbsp;<br class="sm:hx-block hx-hidden" />Route traffic selectively through VPN, WAN, or custom tables.
{{< /hextra/hero-subtitle >}}
</div>

<div class="hx-mb-6">
{{< hextra/hero-button text="Get Started" link="docs/getting-started/" >}}
</div>

<div class="hx-mt-6"></div>

{{< hextra/feature-grid >}}
  {{< hextra/feature-card
    title="Selective Routing"
    subtitle="Route traffic based on domain lists, IP CIDRs, ports, protocols, and source/destination addresses."
    icon="map"
  >}}
  {{< hextra/feature-card
    title="Multiple Outbounds"
    subtitle="Supports interface, routing table, blackhole, ignore, and adaptive urltest outbound types."
    icon="switch-horizontal"
  >}}
  {{< hextra/feature-card
    title="Failover & Health"
    subtitle="urltest outbound probes latency and automatically selects the best child with circuit breaker protection."
    icon="shield-check"
  >}}
  {{< hextra/feature-card
    title="DNS Integration"
    subtitle="Generates dnsmasq configuration to resolve listed domains through the correct DNS server."
    icon="server"
  >}}
  {{< hextra/feature-card
    title="Dual Firewall Backend"
    subtitle="Auto-detects nftables or iptables/ipset. Works on both modern and legacy kernels."
    icon="fire"
  >}}
  {{< hextra/feature-card
    title="REST API"
    subtitle="Built-in HTTP API for health checks, live routing verification, and config updates."
    icon="code"
  >}}
{{< /hextra/feature-grid >}}
