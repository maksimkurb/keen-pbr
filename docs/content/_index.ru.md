---
title: keen-pbr
layout: hextra-home
toc: false
---

<div class="hx:mt-4 hx:mb-6">
{{< hextra/hero-headline >}}
  Выборочная маршрутизация&nbsp;<br class="hx:sm:block hx:hidden" />для Linux-роутеров
{{< /hextra/hero-headline >}}
</div>

<div class="hx:mb-12">
{{< hextra/hero-subtitle >}}
  Направляйте нужный трафик через VPN, WAN или пользовательские таблицы маршрутизации.&nbsp;<br class="hx:sm:block hx:hidden" />Разработано для роутеров OpenWRT, Keenetic® и Netcraze®.
{{< /hextra/hero-subtitle >}}
</div>

<div class="hx:mb-6">
{{< hextra/hero-button text="Перейти к документации" link="docs/getting-started/" >}}
</div>

<div class="hx:mt-6"></div>

{{< hextra/feature-grid >}}

<div class="hextra-feature-card not-prose hx:block hx:relative hx:overflow-hidden hx:rounded-3xl hx:border hx:border-gray-200 hx:dark:border-neutral-800 hx:aspect-auto hx:max-md:min-h-[280px]" style="background: radial-gradient(ellipse at 50% 80%,rgba(59,130,246,0.15),hsla(0,0%,100%,0));display:flex;flex-direction:column;">
<div class="hx:relative hx:w-full hx:p-6" style="flex:1;display:flex;flex-direction:column;">
<h3 class="hx:text-2xl hx:font-medium hx:leading-6 hx:mb-2">Выборочная маршрутизация</h3>
<p class="hx:text-gray-500 hx:dark:text-gray-400 hx:text-sm hx:leading-6">Маршрутизация трафика на основе списков доменов, IP-адресов, портов и адресов. Правила проверяются по порядку; трафик, не попавший ни под одно правило, идёт через обычную системную маршрутизацию.</p>
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
<h3 class="hx:text-2xl hx:font-medium hx:leading-6 hx:mb-2">Веб-интерфейс</h3>
<p class="hx:text-gray-500 hx:dark:text-gray-400 hx:text-sm hx:leading-6">Управление правилами маршрутизации, мониторинг состояния outbounds и перезагрузка конфигурации через браузер — без SSH.</p>
<div style="flex:1;position:relative;overflow:hidden;border-radius:12px 0 0 0;margin:16px -24px -24px 0;min-height:120px;">

<style>
  .kpbr-ui-mock {
    --kpbr-window-border: rgba(139, 92, 246, 0.3);
    --kpbr-window-shadow: -4px -4px 24px rgba(139, 92, 246, 0.12);
    --kpbr-window-bg: linear-gradient(180deg, rgba(255, 255, 255, 0.96), rgba(248, 250, 252, 0.98));
    --kpbr-chrome-bg: rgba(255, 255, 255, 0.8);
    --kpbr-divider: rgba(148, 163, 184, 0.2);
    --kpbr-sidebar-bg: linear-gradient(180deg, rgba(248, 250, 252, 0.9), rgba(241, 245, 249, 0.85));
    --kpbr-main-bg: radial-gradient(circle at top right, rgba(139, 92, 246, 0.12), transparent 34%), linear-gradient(180deg, rgba(255, 255, 255, 0.88), rgba(248, 250, 252, 0.98));
    --kpbr-text: #0f172a;
    --kpbr-muted: #64748b;
    --kpbr-chip-bg: rgba(139, 92, 246, 0.08);
    --kpbr-chip-text: #6d28d9;
    --kpbr-card-bg: rgba(255, 255, 255, 0.8);
    --kpbr-card-bg-soft: rgba(255, 255, 255, 0.78);
    --kpbr-card-bg-strong: rgba(255, 255, 255, 0.82);
    --kpbr-card-border: rgba(148, 163, 184, 0.18);
    --kpbr-input-bg: #ffffff;
    --kpbr-line: rgba(148, 163, 184, 0.18);
    --kpbr-line-active: rgba(59, 130, 246, 0.16);
    --kpbr-green-soft: rgba(16, 185, 129, 0.12);
    --kpbr-green-soft-2: rgba(16, 185, 129, 0.1);
    --kpbr-green-soft-3: rgba(16, 185, 129, 0.07);
    --kpbr-green-soft-4: rgba(16, 185, 129, 0.18);
    --kpbr-green-soft-5: rgba(16, 185, 129, 0.14);
    --kpbr-green-text: #059669;
    --kpbr-green-solid: #10b981;
    --kpbr-blue-grad: linear-gradient(90deg, rgba(59, 130, 246, 0.12), rgba(59, 130, 246, 0.04));
    --kpbr-blue-row: linear-gradient(90deg, rgba(59, 130, 246, 0.12), rgba(255, 255, 255, 0));
    --kpbr-amber-row: linear-gradient(90deg, rgba(245, 158, 11, 0.14), rgba(255, 255, 255, 0));
    --kpbr-amber-soft: rgba(245, 158, 11, 0.16);
  }

  .dark .kpbr-ui-mock,
  [data-theme="dark"] .kpbr-ui-mock {
    --kpbr-window-border: rgba(167, 139, 250, 0.35);
    --kpbr-window-shadow: -6px -6px 28px rgba(15, 23, 42, 0.45);
    --kpbr-window-bg: linear-gradient(180deg, rgba(15, 23, 42, 0.98), rgba(2, 6, 23, 0.98));
    --kpbr-chrome-bg: rgba(15, 23, 42, 0.88);
    --kpbr-divider: rgba(71, 85, 105, 0.45);
    --kpbr-sidebar-bg: linear-gradient(180deg, rgba(15, 23, 42, 0.92), rgba(17, 24, 39, 0.88));
    --kpbr-main-bg: radial-gradient(circle at top right, rgba(139, 92, 246, 0.18), transparent 34%), linear-gradient(180deg, rgba(17, 24, 39, 0.94), rgba(2, 6, 23, 0.98));
    --kpbr-text: #e5e7eb;
    --kpbr-muted: #94a3b8;
    --kpbr-chip-bg: rgba(139, 92, 246, 0.18);
    --kpbr-chip-text: #ddd6fe;
    --kpbr-card-bg: rgba(30, 41, 59, 0.82);
    --kpbr-card-bg-soft: rgba(30, 41, 59, 0.76);
    --kpbr-card-bg-strong: rgba(30, 41, 59, 0.86);
    --kpbr-card-border: rgba(71, 85, 105, 0.5);
    --kpbr-input-bg: rgba(15, 23, 42, 0.78);
    --kpbr-line: rgba(71, 85, 105, 0.52);
    --kpbr-line-active: rgba(96, 165, 250, 0.28);
    --kpbr-green-soft: rgba(16, 185, 129, 0.2);
    --kpbr-green-soft-2: rgba(16, 185, 129, 0.16);
    --kpbr-green-soft-3: rgba(16, 185, 129, 0.14);
    --kpbr-green-soft-4: rgba(16, 185, 129, 0.24);
    --kpbr-green-soft-5: rgba(16, 185, 129, 0.2);
    --kpbr-green-text: #6ee7b7;
    --kpbr-green-solid: #34d399;
    --kpbr-blue-grad: linear-gradient(90deg, rgba(96, 165, 250, 0.22), rgba(96, 165, 250, 0.06));
    --kpbr-blue-row: linear-gradient(90deg, rgba(96, 165, 250, 0.24), rgba(15, 23, 42, 0));
    --kpbr-amber-row: linear-gradient(90deg, rgba(251, 191, 36, 0.24), rgba(15, 23, 42, 0));
    --kpbr-amber-soft: rgba(251, 191, 36, 0.22);
  }
</style>

<div class="kpbr-ui-mock" style="width:120%;max-width:none;transform:translate(5%, 5%);transform-origin:bottom left;">
  <div style="border-radius:14px 0 0 0;border-top:1.5px solid var(--kpbr-window-border);border-left:1.5px solid var(--kpbr-window-border);box-shadow:var(--kpbr-window-shadow);background:var(--kpbr-window-bg);overflow:hidden;">
    <div style="display:flex;align-items:center;gap:6px;padding:8px 10px;border-bottom:1px solid var(--kpbr-divider);background:var(--kpbr-chrome-bg);">
      <span style="width:8px;height:8px;border-radius:50%;background:#f87171;display:inline-block;"></span>
      <span style="width:8px;height:8px;border-radius:50%;background:#fbbf24;display:inline-block;"></span>
      <span style="width:8px;height:8px;border-radius:50%;background:#34d399;display:inline-block;"></span>
      <div style="margin-left:6px;padding:3px 10px;border-radius:999px;background:var(--kpbr-chip-bg);font-size:10px;font-weight:600;color:var(--kpbr-chip-text);">keen-pbr.local</div>
    </div>
    <div style="display:grid;grid-template-columns:102px 1fr;min-height:164px;">
      <div style="padding:12px 10px;background:var(--kpbr-sidebar-bg);border-right:1px solid var(--kpbr-card-border);">
        <div style="display:grid;grid-template-columns:28px 1fr;align-items:center;gap:8px;margin-bottom:12px;">
          <img src="/logo.svg" alt="keen-pbr logo" style="width:28px;height:28px;border-radius:8px;display:block;" />
          <div style="min-width:0;">
            <div style="font-size:10px;font-weight:700;line-height:1.05;color:var(--kpbr-text);white-space:nowrap;">keen-pbr</div>
            <div style="font-size:7px;color:var(--kpbr-muted);margin-top:3px;line-height:1.15;">Get packets sorted</div>
          </div>
        </div>
        <div style="display:grid;gap:6px;">
          <div style="height:8px;border-radius:999px;background:var(--kpbr-line-active);width:70%;"></div>
          <div style="height:8px;border-radius:999px;background:var(--kpbr-line);width:84%;"></div>
          <div style="height:8px;border-radius:999px;background:var(--kpbr-line);width:58%;"></div>
          <div style="height:8px;border-radius:999px;background:var(--kpbr-line);width:76%;"></div>
        </div>
        <div style="margin-top:22px;height:34px;border-radius:10px;background:var(--kpbr-input-bg);border:1px solid var(--kpbr-card-border);"></div>
      </div>
      <div style="padding:14px 14px 12px 14px;display:flex;flex-direction:column;gap:10px;background:var(--kpbr-main-bg);">
        <div style="display:flex;align-items:flex-start;justify-content:space-between;gap:12px;">
          <div>
            <div style="font-size:18px;font-weight:700;line-height:1.1;color:var(--kpbr-text);">Панель управления</div>
            <div style="font-size:10px;color:var(--kpbr-muted);margin-top:4px;">Состояние сервиса, проверки DNS и активные маршруты</div>
          </div>
          <div style="display:flex;gap:6px;align-items:center;">
            <span style="padding:3px 7px;border-radius:999px;background:var(--kpbr-green-soft);color:var(--kpbr-green-text);font-size:9px;font-weight:700;">running</span>
            <span style="width:8px;height:8px;border-radius:50%;background:var(--kpbr-green-solid);box-shadow:0 0 0 4px var(--kpbr-green-soft);display:inline-block;"></span>
          </div>
        </div>
        <div style="display:grid;grid-template-columns:1.1fr 0.9fr;gap:10px;">
          <div style="border:1px solid var(--kpbr-card-border);border-radius:12px;background:var(--kpbr-card-bg);padding:10px;">
            <div style="font-size:10px;color:var(--kpbr-muted);margin-bottom:8px;">Core сервис</div>
            <div style="display:flex;gap:8px;margin-bottom:10px;">
              <div style="flex:1;height:28px;border-radius:9px;background:var(--kpbr-blue-grad);"></div>
              <div style="width:54px;height:28px;border-radius:9px;background:var(--kpbr-green-soft-2);"></div>
            </div>
            <div style="display:flex;gap:6px;">
              <div style="height:22px;flex:1;border-radius:8px;border:1px solid var(--kpbr-card-border);background:var(--kpbr-input-bg);"></div>
              <div style="height:22px;width:22px;border-radius:8px;background:var(--kpbr-chip-bg);"></div>
              <div style="height:22px;width:22px;border-radius:8px;background:var(--kpbr-line-active);"></div>
            </div>
          </div>
          <div style="border:1px solid var(--kpbr-card-border);border-radius:12px;background:var(--kpbr-card-bg-soft);padding:10px;">
            <div style="font-size:10px;color:var(--kpbr-muted);margin-bottom:10px;">Проверка DNS</div>
            <div style="display:flex;align-items:center;gap:8px;padding:10px;border-radius:10px;background:var(--kpbr-green-soft-3);margin-bottom:8px;">
              <span style="width:18px;height:18px;border-radius:50%;border:1.5px solid var(--kpbr-green-solid);display:inline-block;position:relative;flex-shrink:0;">
                <span style="position:absolute;left:4px;top:4px;width:6px;height:3px;border-left:1.5px solid var(--kpbr-green-solid);border-bottom:1.5px solid var(--kpbr-green-solid);transform:rotate(-45deg);"></span>
              </span>
              <div style="height:8px;flex:1;border-radius:999px;background:var(--kpbr-green-soft-4);"></div>
            </div>
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px;">
              <div style="height:20px;border-radius:8px;border:1px solid var(--kpbr-card-border);background:var(--kpbr-input-bg);"></div>
              <div style="height:20px;border-radius:8px;border:1px solid var(--kpbr-card-border);background:var(--kpbr-input-bg);"></div>
            </div>
          </div>
        </div>
        <div style="border:1px solid var(--kpbr-card-border);border-radius:12px;background:var(--kpbr-card-bg-strong);padding:10px 10px 8px 10px;">
          <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:8px;">
            <div style="font-size:10px;color:var(--kpbr-muted);">Состояние outbounds</div>
            <div style="height:8px;width:46px;border-radius:999px;background:var(--kpbr-line);"></div>
          </div>
          <div style="display:grid;gap:6px;">
            <div style="display:grid;grid-template-columns:1fr 44px;gap:8px;align-items:center;">
              <div style="height:18px;border-radius:8px;background:var(--kpbr-blue-row);"></div>
              <div style="height:16px;border-radius:999px;background:var(--kpbr-green-soft-5);"></div>
            </div>
            <div style="display:grid;grid-template-columns:1fr 44px;gap:8px;align-items:center;">
              <div style="height:18px;border-radius:8px;background:var(--kpbr-amber-row);"></div>
              <div style="height:16px;border-radius:999px;background:var(--kpbr-amber-soft);"></div>
            </div>
            <div style="display:grid;grid-template-columns:1fr 44px;gap:8px;align-items:center;">
              <div style="height:18px;border-radius:8px;background:linear-gradient(90deg, var(--kpbr-green-soft), rgba(255, 255, 255, 0));"></div>
              <div style="height:16px;border-radius:999px;background:var(--kpbr-green-soft-5);"></div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>

</div>
</div>
</div>

<div class="hextra-feature-card not-prose hx:block hx:relative hx:overflow-hidden hx:rounded-3xl hx:border hx:border-gray-200 hx:dark:border-neutral-800 hx:aspect-auto hx:max-md:min-h-[280px]" style="background: radial-gradient(ellipse at 50% 80%,rgba(239,68,68,0.15),hsla(0,0%,100%,0));display:flex;flex-direction:column;">
<div class="hx:relative hx:w-full hx:p-6" style="flex:1;display:flex;flex-direction:column;">
<h3 class="hx:text-2xl hx:font-medium hx:leading-6 hx:mb-2">Failover и проверки состояния</h3>
<p class="hx:text-gray-500 hx:dark:text-gray-400 hx:text-sm hx:leading-6">urltest проверяет кандидаты outbound по задержке и выбирает самый быстрый в пределах допуска. Circuit breaker предотвращает флаппинг при устойчивых сбоях.</p>
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
    title="Несколько типов outbound"
    icon="switch-horizontal"
    subtitle="Трафик можно направить на конкретный интерфейс, в существующую таблицу маршрутизации (например, WAN) или полностью заблокировать. Тип <code>urltest</code> измеряет задержку и автоматически выбирает лучший интерфейс."
  >}}
  {{< hextra/feature-card
    title="Интеграция с DNS"
    icon="server"
    subtitle="Генерирует директивы dnsmasq <code>server=</code> и <code>ipset=</code>/<code>nftset=</code>, чтобы разрешённые IP-адреса доменов сразу маршрутизировались через нужный outbound."
  >}}
  {{< hextra/feature-card
    title="Два firewall-бэкенда"
    icon="fire"
    subtitle="Автоматически определяет <code>nftables</code> или <code>iptables</code>. Работает на современных и старых ядрах без изменения конфигурации."
  >}}
  {{< hextra/feature-card
    title="REST API"
    icon="code"
    subtitle="Встроенный HTTP API для проверки состояния, верификации маршрутизации ядра в реальном времени и перезагрузки конфигурации без перезапуска демона."
  >}}
  {{< hextra/feature-card
    title="Автообновление списков"
    icon="refresh"
    subtitle="Удалённые списки доменов и IP загружаются при запуске и обновляются по настраиваемому расписанию, с локальным кэшем в качестве резервного источника."
  >}}
{{< /hextra/feature-grid >}}
