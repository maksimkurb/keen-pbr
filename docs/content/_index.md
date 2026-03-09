---
title: keen-pbr3
layout: hextra-home
toc: false
---

{{< hextra/hero-badge >}}
  <div class="hx:w-2 hx:h-2 hx:rounded-full hx:bg-primary-400"></div>
  <span>Free, open source</span>
  {{< icon name="arrow-circle-right" attributes="height=14" >}}
{{< /hextra/hero-badge >}}

<div class="hx:mt-6 hx:mb-2 hx:flex hx:justify-center">
  <img src="/logo.svg" alt="keen-pbr3 logo" width="120" height="120" style="border-radius:16px;" />
</div>

<div class="hx:mt-4 hx:mb-6">
{{< hextra/hero-headline >}}
  Policy-based routing&nbsp;<br class="hx:sm:block hx:hidden" />for Linux routers
{{< /hextra/hero-headline >}}
</div>

<div class="hx:mb-12">
{{< hextra/hero-subtitle >}}
  Route traffic selectively through VPN, WAN, or custom tables.&nbsp;<br class="hx:sm:block hx:hidden" />Built for OpenWRT, Keenetic&reg; and Netcraze&reg; routers.
{{< /hextra/hero-subtitle >}}
</div>

<div class="hx:mb-6">
{{< hextra/hero-button text="Get Started" link="docs/getting-started/" >}}
</div>

<div class="hx:mt-6"></div>

{{< hextra/feature-grid >}}
  {{< hextra/feature-card
    title="Selective Routing"
    subtitle="Route traffic based on domain lists, IP CIDRs, ports, protocols, and source/destination addresses. First-match rule evaluation with configurable fallback."
    class="hx:aspect-auto hx:md:aspect-[1.1/1] hx:max-md:min-h-[340px]"
    style="background: radial-gradient(ellipse at 50% 80%,rgba(59,130,246,0.15),hsla(0,0%,100%,0));"
  >}}
  {{< hextra/feature-card
    title="Multiple Outbound Types"
    subtitle="Interface, routing table, blackhole, ignore, and adaptive urltest outbounds — each with its own fwmark and kernel routing table entry."
    class="hx:aspect-auto hx:md:aspect-[1.1/1] hx:max-lg:min-h-[340px]"
    style="background: radial-gradient(ellipse at 50% 80%,rgba(16,185,129,0.15),hsla(0,0%,100%,0));"
  >}}
  {{< hextra/feature-card
    title="Failover & Health Checks"
    subtitle="urltest probes candidate outbounds by latency and picks the fastest within tolerance. Circuit breaker prevents flapping under sustained failures."
    class="hx:aspect-auto hx:md:aspect-[1.1/1] hx:max-md:min-h-[340px]"
    style="background: radial-gradient(ellipse at 50% 80%,rgba(239,68,68,0.15),hsla(0,0%,100%,0));"
  >}}
  {{< hextra/feature-card
    title="DNS Integration"
    icon="server"
    subtitle="Generates dnsmasq `server=` and `ipset=`/`nftset=` directives so resolved domain IPs are instantly routed through the correct outbound."
  >}}
  {{< hextra/feature-card
    title="Dual Firewall Backend"
    icon="fire"
    subtitle="Auto-detects nftables or iptables/ipset. Works on both modern and legacy kernels without configuration changes."
  >}}
  {{< hextra/feature-card
    title="REST API"
    icon="code"
    subtitle="Built-in HTTP API for health checks, live kernel routing verification, and hot config reloads — no daemon restart required."
  >}}
  {{< hextra/feature-card
    title="Auto-updating Lists"
    icon="refresh"
    subtitle="Remote domain and IP lists are downloaded at startup and refreshed on a configurable cron schedule, with local cache fallback."
  >}}
  {{< hextra/feature-card
    title="And Much More..."
    icon="sparkles"
    subtitle="fwmark + iproute2 / nftsets & ipsets / circuit breaker / port & address filters / address negation / PID file / configurable cache dir..."
  >}}
{{< /hextra/feature-grid >}}