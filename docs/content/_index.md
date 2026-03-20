---
title: keen-pbr
layout: hextra-home
toc: false
---

{{< hextra/hero-badge >}}
  <div class="hx:w-2 hx:h-2 hx:rounded-full hx:bg-primary-400"></div>
  <span>Free, open source</span>
  {{< icon name="arrow-circle-right" attributes="height=14" >}}
{{< /hextra/hero-badge >}}

<div class="hx:mt-6 hx:mb-2 hx:flex hx:justify-center">
  <img src="/logo.svg" alt="keen-pbr logo" width="120" height="120" style="border-radius:16px;" />
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

<div class="hextra-feature-card not-prose hx:block hx:relative hx:overflow-hidden hx:rounded-3xl hx:border hx:border-gray-200 hx:dark:border-neutral-800 hx:aspect-auto hx:max-md:min-h-[280px]" style="background: radial-gradient(ellipse at 50% 80%,rgba(59,130,246,0.15),hsla(0,0%,100%,0));display:flex;flex-direction:column;">
<div class="hx:relative hx:w-full hx:p-6" style="flex:1;display:flex;flex-direction:column;">
<h3 class="hx:text-2xl hx:font-medium hx:leading-6 hx:mb-2">Selective Routing</h3>
<p class="hx:text-gray-500 hx:dark:text-gray-400 hx:text-sm hx:leading-6">Route traffic based on domain lists, IP CIDRs, ports, and addresses. First-match rule evaluation with configurable fallback.</p>
<div style="flex:1;"></div>
<div class="hx:mt-4">
<svg id="kpbr-sr" viewBox="0 30 300 100" style="width:100%;height:auto;display:block;" aria-hidden="true">
  <g id="kpbr-sr-d1" style="opacity:0.35;transition:opacity 0.4s;">
    <rect x="2" y="40" width="102" height="26" rx="6" style="fill:rgba(59,130,246,0.1);stroke:#3b82f6;stroke-width:1.5;"/>
    <text x="10" y="57" style="font-size:11px;fill:currentColor;font-family:monospace;">google.com</text>
  </g>
  <g id="kpbr-sr-d2" style="opacity:0.35;transition:opacity 0.4s;">
    <rect x="2" y="94" width="102" height="26" rx="6" style="fill:rgba(16,185,129,0.1);stroke:#10b981;stroke-width:1.5;"/>
    <text x="10" y="111" style="font-size:11px;fill:currentColor;font-family:monospace;">internal.site</text>
  </g>
  <g id="kpbr-sr-hub" style="opacity:1;">
    <circle cx="150" cy="80" r="22" style="fill:rgba(128,128,128,0.1);stroke:rgba(128,128,128,0.4);stroke-width:1.5;"/>
    <rect x="140" y="74" width="20" height="12" rx="2" style="fill:none;stroke:currentColor;stroke-width:1.2;opacity:0.7;"/>
    <circle cx="144" cy="80" r="1.5" style="fill:currentColor;opacity:0.7;"/>
    <circle cx="150" cy="80" r="1.5" style="fill:currentColor;opacity:0.7;"/>
    <circle cx="156" cy="80" r="1.5" style="fill:currentColor;opacity:0.7;"/>
    <line x1="150" y1="74" x2="150" y2="68" style="stroke:currentColor;stroke-width:1.2;opacity:0.7;"/>
    <line x1="147" y1="68" x2="153" y2="68" style="stroke:currentColor;stroke-width:1.2;opacity:0.7;"/>
  </g>
  <g id="kpbr-sr-o1" style="opacity:0.35;transition:opacity 0.4s;">
    <rect x="196" y="40" width="102" height="26" rx="6" style="fill:rgba(59,130,246,0.1);stroke:#3b82f6;stroke-width:1.5;"/>
    <text x="205" y="57" style="font-size:11px;fill:currentColor;">WAN</text>
  </g>
  <g id="kpbr-sr-o2" style="opacity:0.35;transition:opacity 0.4s;">
    <rect x="196" y="94" width="102" height="26" rx="6" style="fill:rgba(16,185,129,0.1);stroke:#10b981;stroke-width:1.5;"/>
    <text x="205" y="111" style="font-size:11px;fill:currentColor;">Corp VPN</text>
  </g>
  <path id="kpbr-sr-p1" d="M 104,53 H 111 Q 116,53 116,58 V 75 Q 116,80 121,80 H 128" pathLength="100" style="fill:none;stroke:#3b82f6;stroke-width:2;stroke-linecap:round;stroke-linejoin:round;stroke-dasharray:100;stroke-dashoffset:100;opacity:0;transition:stroke-dashoffset 0.55s ease-in-out,opacity 0.3s;"/>
  <path id="kpbr-sr-p2" d="M 104,107 H 111 Q 116,107 116,102 V 85 Q 116,80 121,80 H 128" pathLength="100" style="fill:none;stroke:#10b981;stroke-width:2;stroke-linecap:round;stroke-linejoin:round;stroke-dasharray:100;stroke-dashoffset:100;opacity:0;transition:stroke-dashoffset 0.55s ease-in-out,opacity 0.3s;"/>
  <path id="kpbr-sr-p3" d="M 172,80 H 179 Q 184,80 184,75 V 58 Q 184,53 189,53 H 196" pathLength="100" style="fill:none;stroke:#3b82f6;stroke-width:2;stroke-linecap:round;stroke-linejoin:round;stroke-dasharray:100;stroke-dashoffset:100;opacity:0;transition:stroke-dashoffset 0.55s ease-in-out,opacity 0.3s;"/>
  <path id="kpbr-sr-p4" d="M 172,80 H 179 Q 184,80 184,85 V 102 Q 184,107 189,107 H 196" pathLength="100" style="fill:none;stroke:#10b981;stroke-width:2;stroke-linecap:round;stroke-linejoin:round;stroke-dasharray:100;stroke-dashoffset:100;opacity:0;transition:stroke-dashoffset 0.55s ease-in-out,opacity 0.3s;"/>
</svg>
</div>
</div>
</div>
<script>
(function(){
  var d1=document.getElementById('kpbr-sr-d1'),d2=document.getElementById('kpbr-sr-d2');
  var o1=document.getElementById('kpbr-sr-o1'),o2=document.getElementById('kpbr-sr-o2');
  var p1=document.getElementById('kpbr-sr-p1'),p2=document.getElementById('kpbr-sr-p2');
  var p3=document.getElementById('kpbr-sr-p3'),p4=document.getElementById('kpbr-sr-p4');
  if(!d1)return;
  function op(el,v){el.style.opacity=v;}
  function sp(p,show){p.style.opacity=show?'1':'0';p.style.strokeDashoffset=show?'0':'100';}
  function reset(){
    op(d1,'0.35');op(d2,'0.35');op(o1,'0.35');op(o2,'0.35');
    sp(p1,false);sp(p2,false);sp(p3,false);sp(p4,false);
  }
  function runGoogle(){
    reset();op(d1,'1');
    setTimeout(function(){sp(p1,true);
      setTimeout(function(){sp(p3,true);
        setTimeout(function(){op(o1,'1');
          setTimeout(function(){setTimeout(runInternal,1200);},900);
        },600);
      },600);
    },400);
  }
  function runInternal(){
    reset();op(d2,'1');
    setTimeout(function(){sp(p2,true);
      setTimeout(function(){sp(p4,true);
        setTimeout(function(){op(o2,'1');
          setTimeout(function(){setTimeout(runGoogle,1200);},900);
        },600);
      },600);
    },400);
  }
  runGoogle();
})();
</script>

<div class="hextra-feature-card not-prose hx:block hx:relative hx:overflow-hidden hx:rounded-3xl hx:border hx:border-gray-200 hx:dark:border-neutral-800 hx:aspect-auto" style="background: radial-gradient(ellipse at 50% 80%,rgba(139,92,246,0.15),hsla(0,0%,100%,0));display:flex;flex-direction:column;max-height:300px;">
<div class="hx:relative hx:w-full hx:p-6" style="flex:1;display:flex;flex-direction:column;">
<h3 class="hx:text-2xl hx:font-medium hx:leading-6 hx:mb-2">Web Interface</h3>
<p class="hx:text-gray-500 hx:dark:text-gray-400 hx:text-sm hx:leading-6">Manage routing rules, monitor outbound health, and reload configuration from a clean browser-based dashboard — no SSH required.</p>
<div style="flex:1;position:relative;overflow:hidden;border-radius:12px 0 0 0;margin:16px -24px -24px 0;min-height:120px;">
<img src="https://picsum.photos/seed/keen-pbr-webui/600/400" alt="Web interface screenshot" style="width:120%;max-width:none;height:auto;display:block;border-radius:10px 0 0 0;border-top:1.5px solid rgba(139,92,246,0.3);border-left:1.5px solid rgba(139,92,246,0.3);transform:translate(8%, 8%);box-shadow:-4px -4px 24px rgba(139,92,246,0.12);" />
</div>
</div>
</div>

<div class="hextra-feature-card not-prose hx:block hx:relative hx:overflow-hidden hx:rounded-3xl hx:border hx:border-gray-200 hx:dark:border-neutral-800 hx:aspect-auto hx:max-md:min-h-[280px]" style="background: radial-gradient(ellipse at 50% 80%,rgba(239,68,68,0.15),hsla(0,0%,100%,0));display:flex;flex-direction:column;">
<div class="hx:relative hx:w-full hx:p-6" style="flex:1;display:flex;flex-direction:column;">
<h3 class="hx:text-2xl hx:font-medium hx:leading-6 hx:mb-2">Failover &amp; Health Checks</h3>
<p class="hx:text-gray-500 hx:dark:text-gray-400 hx:text-sm hx:leading-6">urltest probes candidate outbounds by latency and picks the fastest within tolerance. Circuit breaker prevents flapping under sustained failures.</p>
<div style="flex:1;"></div>
<div class="hx:mt-4">
  <div style="border:1.5px solid rgba(128,128,128,0.25);border-radius:10px;padding:8px 10px;background:rgba(128,128,128,0.06);">
    <div style="font-size:9px;color:#6b7280;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:8px;font-weight:600;">urltest outbound</div>
    <div id="kpbr-hc-r1" style="display:flex;align-items:center;gap:6px;padding:5px 8px;border-radius:7px;border:1.5px solid #10b981;margin-bottom:6px;transition:border-color 0.4s;">
      <span id="kpbr-hc-dot1" style="width:8px;height:8px;border-radius:50%;background:#10b981;display:inline-block;flex-shrink:0;transition:background 0.4s;"></span>
      <span style="font-size:11px;font-family:monospace;flex:1;">eth0</span>
      <span id="kpbr-hc-p1" style="font-size:11px;color:#6b7280;min-width:32px;text-align:right;">87ms</span>
      <span id="kpbr-hc-b1" style="font-size:9px;background:#10b981;color:#fff;border-radius:4px;padding:1px 5px;font-weight:600;transition:opacity 0.4s;white-space:nowrap;">BEST</span>
    </div>
    <div id="kpbr-hc-r2" style="display:flex;align-items:center;gap:6px;padding:5px 8px;border-radius:7px;border:1.5px solid rgba(128,128,128,0.25);transition:border-color 0.4s;">
      <span id="kpbr-hc-dot2" style="width:8px;height:8px;border-radius:50%;background:#10b981;display:inline-block;flex-shrink:0;transition:background 0.4s;"></span>
      <span style="font-size:11px;font-family:monospace;flex:1;">wlan0</span>
      <span id="kpbr-hc-p2" style="font-size:11px;color:#6b7280;min-width:32px;text-align:right;">142ms</span>
      <span id="kpbr-hc-b2" style="font-size:9px;background:#10b981;color:#fff;border-radius:4px;padding:1px 5px;font-weight:600;opacity:0;transition:opacity 0.4s;white-space:nowrap;">BEST</span>
    </div>
  </div>
</div>
</div>
</div>
<script>
(function(){
  var dot1=document.getElementById('kpbr-hc-dot1'),dot2=document.getElementById('kpbr-hc-dot2');
  var p1=document.getElementById('kpbr-hc-p1'),p2=document.getElementById('kpbr-hc-p2');
  var r1=document.getElementById('kpbr-hc-r1'),r2=document.getElementById('kpbr-hc-r2');
  var b1=document.getElementById('kpbr-hc-b1'),b2=document.getElementById('kpbr-hc-b2');
  if(!dot1)return;
  var state={v1:true,v2:true,ms1:87,ms2:142};
  function rnd(){return Math.floor(Math.random()*250)+50;}
  function render(){
    var m1=state.v1?state.ms1:Infinity,m2=state.v2?state.ms2:Infinity;
    var best1=m1<=m2;
    r1.style.borderColor=state.v1?(best1?'#10b981':'rgba(128,128,128,0.25)'):'#ef4444';
    r2.style.borderColor=state.v2?(best1?'rgba(128,128,128,0.25)':'#10b981'):'#ef4444';
    b1.style.opacity=(state.v1&&best1)?'1':'0';
    b2.style.opacity=(state.v2&&!best1)?'1':'0';
    p1.textContent=state.v1?state.ms1+'ms':'–';
    p2.textContent=state.v2?state.ms2+'ms':'–';
    dot1.style.background=state.v1?'#10b981':'#ef4444';
    dot2.style.background=state.v2?'#10b981':'#ef4444';
  }
  setInterval(function(){
    if(state.v1)state.ms1=rnd();
    if(state.v2)state.ms2=rnd();
    render();
  },2000);
  setInterval(function(){
    var which=Math.random()<0.5?1:2;
    if(which===1)state.v1=false; else state.v2=false;
    render();
    setTimeout(function(){
      if(which===1){state.v1=true;state.ms1=rnd();}
      else{state.v2=true;state.ms2=rnd();}
      render();
    },2000);
  },5000);
  render();
})();
</script>
  {{< hextra/feature-card
    title="Multiple Outbound Types"
    icon="switch-horizontal"
    subtitle="Route traffic to a specific interface, to an existing routing table (e.g. WAN), or block it completely. Use <code>urltest</code> to monitor latency and automatically select the best interface."
  >}}
  {{< hextra/feature-card
    title="DNS Integration"
    icon="server"
    subtitle="Generates dnsmasq `server=` and `ipset=` directives so resolved domain IPs are instantly routed through the correct outbound."
  >}}
  {{< hextra/feature-card
    title="Dual Firewall Backend"
    icon="fire"
    subtitle="Auto-detects <code>nftables</code> or <code>iptables</code>. Works on both modern and legacy kernels without configuration changes."
  >}}
  {{< hextra/feature-card
    title="REST API"
    icon="code"
    subtitle="Built-in HTTP API for health checks, live kernel routing verification, and hot config reloads — no daemon restart required."
  >}}
  {{< hextra/feature-card
    title="Auto-updating lists"
    icon="refresh"
    subtitle="Remote domain and IP lists are downloaded at startup and refreshed on a configurable cron schedule, with local cache fallback."
  >}}
{{< /hextra/feature-grid >}}
