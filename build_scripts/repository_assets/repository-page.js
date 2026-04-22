/* ============================================================
   keen-pbr repository installer — Alpine.js rewrite
   Features:
     • Alpine.js reactivity
     • Fuzzy-search inline Selector component
     • Full i18n (en / ru) with ?lang= query param support
     • Light/Dark theme responsive
   ============================================================ */

(function () {
  "use strict";

  /* ──────────────────────────────────────────────────────────
     i18n
  ────────────────────────────────────────────────────────── */
  var TRANSLATIONS = {
    en: {
      pageTitle: "keen-pbr repository",
      pageSubtitle:
        "Select your OS family and architecture to generate the exact commands needed to add this repository and install keen-pbr.",
      langToggle: "RU",

      chooseTarget: "Choose your target",
      selectOs: "Select OS",
      selectVersion: "Select version",
      selectArch: "Select architecture",

      searchOs: "Search OS…",
      searchVersion: "Search version…",
      searchArch: "Search architecture…",
      placeholderOs: "Select OS…",
      placeholderVersion: "Select version…",
      placeholderArch: "Select architecture…",
      noResults: "No matches",

      selectOsFirst: "Select OS first…",
      selectVersionFirst: "Select version first…",

      desc_keenetic: "Entware opkg feed for Keenetic and Netcraze routers.",
      desc_openwrtOpkg: "Classic opkg/ipk feed for older OpenWrt releases.",
      desc_openwrtApk: "APK repository for newer OpenWrt releases.",
      desc_debian: "APT repository for Debian systems.",
      sidebarDefault:
        "Pick an operating system to unlock the matching version and architecture options.",

      chipRepository: "Repository",
      chipSource: "Source",
      chipPullRequest: "Pull Request",

      installInstructions: "Install instructions",
      installSubtitle:
        "Commands update immediately when you change the operating system, version, or architecture.",

      emptySelectOs: "Select OS",
      emptySelectOsMsg:
        "Choose an operating system on the left to generate installation commands.",
      emptySelectOptions: "Select options",
      emptySelectOptionsMsg:
        "Finish choosing the version and architecture on the left to see the exact commands.",
      emptyNoPackages:
        "No packages are currently published for this selection.",

      installCardHeading: "OS: {name}; version: {version}; architecture: {arch}",
      installCardIntro:
        "Use these commands to trust the repository, register the feed, and install keen-pbr.",

      stepInstallKeys: "1. Install signing keys",
      stepInstallWget: "1. Install wget-ssl for HTTPS support",
      stepAddRepo: "2. Add repository / feed",
      stepInstall: "3. Update and install keen-pbr",

      copy: "Copy",
      copied: "Copied",
      copyFailed: "Failed",

      keysNoSigning:
        "# No signing key installation is required for Keenetic / Netcraze feeds",
      keysLoadError: "# Failed to load /keys/keys.json\n# {error}",
      keysLoading: "# Loading signing key information from /keys/keys.json",
    },

    ru: {
      pageTitle: "репозиторий keen-pbr",
      pageSubtitle:
        "Выберите семейство ОС и архитектуру, чтобы получить точные команды для добавления репозитория и установки keen-pbr.",
      langToggle: "EN",

      chooseTarget: "Выберите цель",
      selectOs: "Операционная система",
      selectVersion: "Версия",
      selectArch: "Архитектура",

      searchOs: "Поиск ОС…",
      searchVersion: "Поиск версии…",
      searchArch: "Поиск архитектуры…",
      placeholderOs: "Выберите ОС…",
      placeholderVersion: "Выберите версию…",
      placeholderArch: "Выберите архитектуру…",
      noResults: "Нет совпадений",

      selectOsFirst: "Сначала выберите ОС…",
      selectVersionFirst: "Сначала выберите версию…",

      desc_keenetic: "Канал Entware opkg для роутеров Keenetic и Netcraze.",
      desc_openwrtOpkg: "Классический канал opkg/ipk для старых релизов OpenWrt.",
      desc_openwrtApk: "Репозиторий APK для новых релизов OpenWrt.",
      desc_debian: "APT-репозиторий для Debian.",
      sidebarDefault:
        "Выберите операционную систему, чтобы разблокировать версии и архитектуры.",

      chipRepository: "Репозиторий",
      chipSource: "Источник",
      chipPullRequest: "Pull Request",

      installInstructions: "Инструкция по установке",
      installSubtitle:
        "Команды обновляются мгновенно при изменении ОС, версии или архитектуры.",

      emptySelectOs: "Выберите ОС",
      emptySelectOsMsg:
        "Выберите операционную систему слева, чтобы сгенерировать команды установки.",
      emptySelectOptions: "Выберите параметры",
      emptySelectOptionsMsg:
        "Завершите выбор версии и архитектуры слева, чтобы увидеть точные команды.",
      emptyNoPackages: "Для выбранной конфигурации пакеты пока не опубликованы.",

      installCardHeading: "ОС: {name}; версия: {version}; архитектура: {arch}",
      installCardIntro:
        "Используйте эти команды, чтобы добавить ключ, зарегистрировать репозиторий и установить keen-pbr.",

      stepInstallKeys: "1. Установить ключ подписи",
      stepInstallWget: "1. Установить wget-ssl для поддержки HTTPS",
      stepAddRepo: "2. Добавить репозиторий / канал",
      stepInstall: "3. Обновить и установить keen-pbr",

      copy: "Копировать",
      copied: "Скопировано",
      copyFailed: "Ошибка",

      keysNoSigning:
        "# Установка ключа подписи не требуется для каналов Keenetic / Netcraze",
      keysLoadError:
        "# Не удалось загрузить /keys/keys.json\n# {error}",
      keysLoading:
        "# Загрузка информации о ключе подписи из /keys/keys.json",
    },
  };

  /* ──────────────────────────────────────────────────────────
     Systems catalogue
  ────────────────────────────────────────────────────────── */
  var SYSTEMS =[
    { id: "keenetic",    label: "Keenetic / Netcraze", name: "Keenetic",  catalogKey: "keenetic"    },
    { id: "openwrtOpkg", label: "OpenWrt 24.x and lower", name: "OpenWrt", catalogKey: "openwrtOpkg" },
    { id: "openwrtApk",  label: "OpenWrt 25.x+",       name: "OpenWrt",  catalogKey: "openwrtApk"  },
    { id: "debian",      label: "Debian",               name: "Debian",   catalogKey: "debian"      },
  ];

  /* ──────────────────────────────────────────────────────────
     Helpers
  ────────────────────────────────────────────────────────── */
  function fuzzyMatch(query, text) {
    if (!query) return true;
    var q = query.toLowerCase();
    var t = text.toLowerCase();
    return t.includes(q);
  }

  function getEntries(config, systemId) {
    var sys = SYSTEMS.find(function (s) { return s.id === systemId; });
    if (!sys) return[];
    var entries = (config.catalog || {})[sys.catalogKey];
    return Array.isArray(entries) ? entries.slice() :[];
  }

  function uniqueVersions(entries) {
    return Array.from(new Set(entries.map(function (e) { return e.version; }))).sort();
  }

  function uniqueArchitectures(entries, version) {
    return Array.from(
      new Set(
        entries
          .filter(function (e) { return e.version === version; })
          .map(function (e) { return e.arch; })
      )
    ).sort();
  }

  /* ──────────────────────────────────────────────────────────
     Command-generation helpers
  ────────────────────────────────────────────────────────── */
  function keyUrlForSystem(systemId, keysManifest) {
    if (!keysManifest) return "";
    if (systemId === "openwrtOpkg") return (keysManifest.openwrt_opkg || {}).key || "";
    if (systemId === "openwrtApk")  return (keysManifest.openwrt_apk  || {}).key || "";
    if (systemId === "debian")      return (keysManifest.debian        || {}).key || "";
    return "";
  }

  function keyBlock(systemId, keysManifest, keysError, t) {
    if (systemId === "keenetic") return t("keysNoSigning");
    if (keysError) return t("keysLoadError").replace("{error}", keysError);
    var url = keyUrlForSystem(systemId, keysManifest);
    if (!url) return t("keysLoading");

    if (systemId === "openwrtOpkg") {
      return "wget " + url + " -O /tmp/openwrt_opkg_public.key\nopkg-key add /tmp/openwrt_opkg_public.key";
    }
    if (systemId === "openwrtApk") {
      return[
        "wget " + url + " -O /etc/apk/keys/openwrt_apk_public.pem",
        "grep -qxF '/etc/apk/keys/openwrt_apk_public.pem' /etc/sysupgrade.conf || echo '/etc/apk/keys/openwrt_apk_public.pem' >> /etc/sysupgrade.conf",
      ].join("\n");
    }
    return[
      "wget " + url + " -O /usr/share/keyrings/keen-pbr-archive-keyring.asc",
      "chmod 0644 /usr/share/keyrings/keen-pbr-archive-keyring.asc",
    ].join("\n");
  }

  function addRepoBlock(systemId, entry) {
    if (systemId === "keenetic")    return "printf '%s\\n' '" + entry.feedLine + "' > /opt/etc/opkg/keen-pbr.conf";
    if (systemId === "openwrtOpkg") return "printf '%s\\n' '" + entry.feedLine + "' > /etc/opkg/keen-pbr.conf";
    if (systemId === "openwrtApk") {
      return[
        "mkdir -p /etc/apk/repositories.d",
        "printf '%s\\n' '" + entry.repositoryUrl + "' > /etc/apk/repositories.d/keen-pbr.list",
      ].join("\n");
    }
    return "printf '%s\\n' '" + entry.sourceLine + "' > /etc/apt/sources.list.d/keen-pbr.list";
  }

  function installBlock(systemId) {
    if (systemId === "debian")     return "apt update\napt install keen-pbr";
    if (systemId === "openwrtApk") return "apk update\napk add keen-pbr";
    return "opkg update\nopkg install keen-pbr";
  }

  function preRepoBlock() {
    return "opkg update\nopkg install wget-ssl";
  }

  /* ──────────────────────────────────────────────────────────
     Alpine.js component
  ────────────────────────────────────────────────────────── */
  function repositoryApp(config) {
    return {
      lang: (function () {
        if (typeof window !== "undefined") {
          var params = new URLSearchParams(window.location.search);
          var l = params.get("lang");
          if (l === "ru" || l === "en") return l;
        }
        return "en";
      })(),
      
      t: function (key) {
        var dict = TRANSLATIONS[this.lang] || TRANSLATIONS.en;
        return Object.prototype.hasOwnProperty.call(dict, key)
          ? dict[key]
          : (TRANSLATIONS.en[key] || key);
      },
      
      toggleLang: function () {
        this.lang = this.lang === "en" ? "ru" : "en";
        // Update URL query string persistently
        if (typeof window !== "undefined" && window.history && window.history.replaceState) {
          var url = new URL(window.location);
          url.searchParams.set("lang", this.lang);
          window.history.replaceState({}, "", url);
        }
      },

      selectedSystem: "",
      selectedVersion: "",
      selectedArch: "",

      keysManifest: null,
      keysError: "",
      config: config,

      get systemDef() {
        return SYSTEMS.find(function (s) { return s.id === this.selectedSystem; }.bind(this)) || null;
      },
      get systemEntries() {
        if (!this.selectedSystem) return[];
        return getEntries(this.config, this.selectedSystem);
      },
      get versions() {
        return uniqueVersions(this.systemEntries);
      },
      get arches() {
        if (!this.selectedVersion) return[];
        return uniqueArchitectures(this.systemEntries, this.selectedVersion);
      },
      get selectedEntry() {
        if (!this.selectedVersion || !this.selectedArch) return null;
        return this.systemEntries.find(function (e) {
          return e.version === this.selectedVersion && e.arch === this.selectedArch;
        }.bind(this)) || null;
      },

      systemOptions: function () {
        return SYSTEMS.map(function (s) {
          return {
            value: s.id,
            label: s.label,
            disabled: getEntries(this.config, s.id).length === 0,
          };
        }.bind(this));
      },

      stepKeyCode: function () { return keyBlock(this.selectedSystem, this.keysManifest, this.keysError, this.t.bind(this)); },
      stepPreRepoCode: function () { return preRepoBlock(); },
      stepAddRepoCode: function () { return this.selectedEntry ? addRepoBlock(this.selectedSystem, this.selectedEntry) : ""; },
      stepInstallCode: function () { return installBlock(this.selectedSystem); },

      installCardTitle: function () {
        if (!this.systemDef || !this.selectedEntry) return "";
        return this.t("installCardHeading")
          .replace("{name}", this.systemDef.name)
          .replace("{version}", this.selectedEntry.version)
          .replace("{arch}", this.selectedEntry.arch);
      },

      showKeynetic: function () { return this.selectedSystem === "keenetic"; },
      showKeyStep: function ()   { return !!this.selectedSystem && this.selectedSystem !== "keenetic"; },

      metaChips: function () {
        var cfg = this.config;
        var chips =[{ label: this.t("chipRepository"), href: null, value: cfg.baseUrl }];
        var src = cfg.source || {};
        if (src.refUrl && src.refLabel) chips.push({ label: this.t("chipSource"), href: src.refUrl, value: src.refLabel });
        if (src.prUrl && src.prNumber) chips.push({ label: this.t("chipPullRequest"), href: src.prUrl, value: "#" + src.prNumber });
        return chips;
      },

      sidebarDescription: function () {
        if (!this.systemDef) return this.t("sidebarDefault");
        return this.t("desc_" + this.selectedSystem);
      },

      copyCode: function (code, btn) {
        var self = this;
        function flash(label) {
          btn.textContent = label;
          setTimeout(function () { btn.textContent = self.t("copy"); }, 1400);
        }
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(code)
            .then(function () { flash(self.t("copied")); })
            .catch(function () { flash(self.t("copyFailed")); });
        } else {
          var ta = document.createElement("textarea");
          ta.value = code;
          ta.style.cssText = "position:absolute;left:-9999px";
          document.body.appendChild(ta);
          ta.select();
          document.execCommand("copy") ? flash(self.t("copied")) : flash(self.t("copyFailed"));
          document.body.removeChild(ta);
        }
      },

      init: function () {
        var self = this;
        fetch("/keys/keys.json", { cache: "no-store" })
          .then(function (r) {
            if (!r.ok) throw new Error("Unable to fetch /keys/keys.json");
            return r.json();
          })
          .then(function (manifest) {
            self.keysManifest = manifest;
            self.keysError = "";
          })
          .catch(function (err) {
            self.keysManifest = null;
            self.keysError = err && err.message ? err.message : "Unable to load signing key metadata.";
          });
      },

      onSystemChange: function () {
        this.selectedVersion = "";
        this.selectedArch = "";
      },
      onVersionChange: function () {
        this.selectedArch = "";
      },
    };
  }

  /* ──────────────────────────────────────────────────────────
     Fuzzy Selector Alpine component
  ────────────────────────────────────────────────────────── */
  function selectorComponent(opts) {
    return {
      open: false,
      query: "",
      focusedIdx: -1,

      get pulse() { return opts.pulse ? opts.pulse() : false; },

      get filtered() {
        var q = this.query;
        return this.getOptions().filter(function (o) {
          return fuzzyMatch(q, o.label);
        });
      },
      get displayLabel() {
        var v = this.getValue();
        if (!v) return "";
        var opt = this.getOptions().find(function (o) { return o.value === v; });
        return opt ? opt.label : v;
      },

      toggle: function () {
        if (this.disabled) return;
        this.open = !this.open;
        if (this.open) {
          this.query = "";
          this.focusedIdx = -1;
          var self = this;
          this.$nextTick(function () {
            var inp = self.$el.querySelector(".sel-search-inline");
            if (inp) inp.focus();
            
            if (window.innerWidth <= 860) {
              setTimeout(function () {
                self.$el.scrollIntoView({ behavior: 'smooth', block: 'start' });
              }, 50);
            }
          });
        }
      },
      close: function () { this.open = false; this.query = ""; this.focusedIdx = -1; },
      select: function (value) {
        this.setValue(value);
        this.close();
      },
      onKey: function (e) {
        if (!this.open) {
          if (e.key === "Enter" || e.key === " " || e.key === "ArrowDown") { e.preventDefault(); this.toggle(); }
          return;
        }
        if (e.key === "Escape") { this.close(); return; }
        if (e.key === "ArrowDown") { e.preventDefault(); this.focusedIdx = Math.min(this.focusedIdx + 1, this.filtered.length - 1); }
        if (e.key === "ArrowUp")   { e.preventDefault(); this.focusedIdx = Math.max(this.focusedIdx - 1, 0); }
        if (e.key === "Enter") {
          e.preventDefault();
          if (this.focusedIdx >= 0 && this.filtered[this.focusedIdx]) {
            this.select(this.filtered[this.focusedIdx].value);
          }
        }
      },

      getValue:       opts.getValue,
      setValue:       opts.setValue,
      getOptions:     opts.getOptions,
      get placeholder()       { return opts.placeholder(); },
      get searchPlaceholder() { return opts.searchPlaceholder(); },
      get disabled()          { return opts.disabled(); },
      get noResultsText()     { return opts.noResults(); },
    };
  }

  /* ──────────────────────────────────────────────────────────
     Bootstrap: register Alpine components and inject HTML
  ────────────────────────────────────────────────────────── */
  function renderRepositoryInstructions(config) {
    if (!window.Alpine) {
      var script = document.createElement("script");
      script.src = "https://cdn.jsdelivr.net/npm/alpinejs@3.x.x/dist/cdn.min.js";
      script.defer = true;
      script.addEventListener("load", function () { boot(config); });
      document.head.appendChild(script);
    } else {
      boot(config);
    }
  }

  function boot(config) {
    var Alpine = window.Alpine;
    Alpine.data("repositoryApp", function () { return repositoryApp(config); });
    Alpine.data("selector", function (opts) { return selectorComponent(opts); });

    injectStyles();

    var root = document.getElementById("app");
    if (!root) return;
    root.innerHTML = buildHTML();

    Alpine.start();
  }

  /* ──────────────────────────────────────────────────────────
     HTML template
  ────────────────────────────────────────────────────────── */
  function buildHTML() {
    return `
<div x-data="repositoryApp" x-init="init()" class="page-shell">

  <!-- ═══ HERO ═══ -->
  <section class="hero">
    <button class="lang-btn hero-lang" @click="toggleLang()" x-text="t('langToggle')" :aria-label="lang === 'en' ? 'Switch to Russian' : 'Switch to English'"></button>
    <h1 x-text="t('pageTitle')"></h1>
    <p x-text="t('pageSubtitle')"></p>
    <div class="hero-meta">
      <template x-for="chip in metaChips()" :key="chip.label">
        <a class="meta-chip"
           :class="{ 'meta-chip--static': !chip.href }"
           :href="chip.href"
           :target="chip.href ? '_blank' : undefined"
           rel="noopener">
          <strong x-text="chip.label"></strong>
          <span x-text="chip.value"></span>
        </a>
      </template>
    </div>
  </section>

  <!-- ═══ LAYOUT ═══ -->
  <div class="layout-grid">

    <!-- ─── SIDEBAR ─── -->
    <aside class="panel">
      <div class="panel-content">
        <h2 x-text="t('chooseTarget')"></h2>

        <div class="selector-stack">
          <!-- OS selector -->
          <div class="field-group">
            <label class="field-label" x-text="t('selectOs')"></label>
            <div x-data="selector({
              getValue:         () => selectedSystem,
              setValue:         (v) => { selectedSystem = v; onSystemChange(); },
              getOptions:       () => systemOptions(),
              placeholder:      () => t('placeholderOs'),
              searchPlaceholder:() => t('searchOs'),
              disabled:         () => false,
              noResults:        () => t('noResults'),
              pulse:            () => !selectedSystem
            })">
              ${selectorHTML()}
            </div>
          </div>

          <!-- Version selector -->
          <div class="field-group">
            <label class="field-label" x-text="t('selectVersion')"></label>
            <div x-data="selector({
              getValue:         () => selectedVersion,
              setValue:         (v) => { selectedVersion = v; onVersionChange(); },
              getOptions:       () => versions.map(v => ({ value: v, label: v })),
              placeholder:      () => selectedSystem ? t('placeholderVersion') : t('selectOsFirst'),
              searchPlaceholder:() => t('searchVersion'),
              disabled:         () => !selectedSystem || versions.length === 0,
              noResults:        () => t('noResults'),
              pulse:            () => selectedSystem && !selectedVersion
            })">
              ${selectorHTML()}
            </div>
          </div>

          <!-- Arch selector -->
          <div class="field-group">
            <label class="field-label" x-text="t('selectArch')"></label>
            <div x-data="selector({
              getValue:         () => selectedArch,
              setValue:         (v) => { selectedArch = v; },
              getOptions:       () => arches.map(a => ({ value: a, label: a })),
              placeholder:      () => selectedVersion ? t('placeholderArch') : (selectedSystem ? t('selectVersionFirst') : t('selectOsFirst')),
              searchPlaceholder:() => t('searchArch'),
              disabled:         () => !selectedVersion || arches.length === 0,
              noResults:        () => t('noResults'),
              pulse:            () => selectedSystem && selectedVersion && !selectedArch
            })">
              ${selectorHTML()}
            </div>
          </div>
        </div>

        <div class="selector-note" x-text="sidebarDescription()"></div>
        <div class="status-note" x-show="keysError" x-text="keysError"></div>
      </div>
    </aside>

    <!-- ─── INSTRUCTIONS ─── -->
    <section class="panel panel--main">
      <div class="panel-content">
        <div class="instruction-header">
          <h2 x-text="t('installInstructions')"></h2>
          <p x-text="t('installSubtitle')"></p>
        </div>

        <div class="install-list">
          <template x-if="!selectedSystem">
            <div class="empty-state">
              <strong x-text="t('emptySelectOs')"></strong>
              <p x-text="t('emptySelectOsMsg')"></p>
            </div>
          </template>

          <template x-if="selectedSystem && (!selectedVersion || !selectedArch)">
            <div class="empty-state">
              <strong x-text="t('emptySelectOptions')"></strong>
              <p x-text="t('emptySelectOptionsMsg')"></p>
            </div>
          </template>

          <template x-if="selectedSystem && selectedVersion && selectedArch && !selectedEntry">
            <div class="empty-state">
              <p x-text="t('emptyNoPackages')"></p>
            </div>
          </template>

          <template x-if="selectedEntry">
            <article class="install-card">
              <h3 x-text="installCardTitle()"></h3>
              <p x-text="t('installCardIntro')"></p>
              <div class="command-grid">
                <!-- Step 1a -->
                <template x-if="showKeyStep()">
                  <section class="command-card">
                    <header>
                      <strong x-text="t('stepInstallKeys')"></strong>
                      <button class="copy-btn" type="button" @click="copyCode(stepKeyCode(), $el)" x-text="t('copy')"></button>
                    </header>
                    <pre><code x-text="stepKeyCode()"></code></pre>
                  </section>
                </template>

                <!-- Step 1b -->
                <template x-if="showKeynetic()">
                  <section class="command-card">
                    <header>
                      <strong x-text="t('stepInstallWget')"></strong>
                      <button class="copy-btn" type="button" @click="copyCode(stepPreRepoCode(), $el)" x-text="t('copy')"></button>
                    </header>
                    <pre><code x-text="stepPreRepoCode()"></code></pre>
                  </section>
                </template>

                <!-- Step 2 -->
                <section class="command-card">
                  <header>
                    <strong x-text="t('stepAddRepo')"></strong>
                    <button class="copy-btn" type="button" @click="copyCode(stepAddRepoCode(), $el)" x-text="t('copy')"></button>
                  </header>
                  <pre><code x-text="stepAddRepoCode()"></code></pre>
                </section>

                <!-- Step 3 -->
                <section class="command-card">
                  <header>
                    <strong x-text="t('stepInstall')"></strong>
                    <button class="copy-btn" type="button" @click="copyCode(stepInstallCode(), $el)" x-text="t('copy')"></button>
                  </header>
                  <pre><code x-text="stepInstallCode()"></code></pre>
                </section>
              </div>
            </article>
          </template>
        </div>
      </div>
    </section>

  </div>
</div>
    `;
  }

  function selectorHTML() {
    return `
      <div class="sel-wrap" @keydown="onKey($event)" @click.away="close()">
        <!-- trigger / input -->
        <div
          class="sel-trigger"
          :class="{ 'sel-trigger--open': open, 'sel-trigger--disabled': disabled, 'sel-trigger--filled': !!getValue(), 'sel-trigger--pulse': pulse && !open }"
          role="button"
          tabindex="0"
          @click="!disabled && toggle()"
          :aria-expanded="open.toString()"
          aria-haspopup="listbox">
          
          <span class="sel-value" x-show="!open" x-text="displayLabel || placeholder"></span>
          
          <input
            x-show="open"
            class="sel-search-inline"
            type="text"
            autocomplete="off"
            :placeholder="searchPlaceholder"
            x-model="query"
            @click.stop
            @keydown.escape.stop="close()"
          />

          <svg class="sel-chevron" :class="{ 'sel-chevron--up': open }" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="2">
            <polyline points="5,8 10,13 15,8"></polyline>
          </svg>
        </div>

        <!-- dropdown -->
        <div class="sel-dropdown" x-show="open" x-transition:enter="sel-enter" x-transition:enter-start="sel-enter-from" x-transition:enter-end="sel-enter-to" x-transition:leave="sel-leave" x-transition:leave-start="sel-leave-from" x-transition:leave-end="sel-leave-to">
          <ul class="sel-list" role="listbox">
            <template x-for="(opt, idx) in filtered" :key="opt.value">
              <li
                class="sel-option"
                :class="{
                  'sel-option--selected': opt.value === getValue(),
                  'sel-option--focused': idx === focusedIdx,
                  'sel-option--disabled': opt.disabled
                }"
                role="option"
                :aria-selected="(opt.value === getValue()).toString()"
                @click.stop="!opt.disabled && select(opt.value)"
                @mouseenter="focusedIdx = idx">
                <span x-text="opt.label"></span>
                <svg x-show="opt.value === getValue()" class="sel-check" viewBox="0 0 16 16" fill="currentColor">
                  <path d="M2 8l4 4 8-8" stroke="currentColor" stroke-width="2" fill="none" stroke-linecap="round"/>
                </svg>
              </li>
            </template>
            <template x-if="filtered.length === 0">
              <li class="sel-option sel-option--empty" x-text="noResultsText"></li>
            </template>
          </ul>
        </div>
      </div>
    `;
  }

  /* ──────────────────────────────────────────────────────────
     Styles
  ────────────────────────────────────────────────────────── */
  function injectStyles() {
    if (document.getElementById("kpbr-styles")) return;
    var style = document.createElement("style");
    style.id = "kpbr-styles";
    style.textContent = CSS;
    document.head.appendChild(style);
  }

  var CSS = `
/* ── Reset / base ── */
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

:root {
  color-scheme: light dark;
  --bg:       #f8fafc;
  --surface:  #ffffff;
  --surface2: #f1f5f9;
  --border:   #e2e8f0;
  --accent:   #2563eb;
  --accent2:  #8b5cf6;
  --text:     #0f172a;
  --text-dim: #64748b;
  --code-bg:  #f8fafc;
  --green:    #059669;
  --error-bg: #fef2f2;
  --error-border: #fecaca;
  --error-text: #dc2626;
  --radius:   10px;
  --font-mono: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace;
  --font-ui:  'DM Sans', 'Sora', system-ui, sans-serif;
  --font-hero:'Syne', 'DM Sans', sans-serif;
  transition: background 0.3s, color 0.3s;
}

@media (prefers-color-scheme: dark) {
  :root {
    --bg:       #0d0f12;
    --surface:  #141720;
    --surface2: #1c2030;
    --border:   #2a3048;
    --accent:   #4f8cff;
    --accent2:  #a78bfa;
    --text:     #e2e8f0;
    --text-dim: #8492a6;
    --code-bg:  #0a0c10;
    --green:    #34d399;
    --error-bg: rgba(255, 80, 80, 0.08);
    --error-border: rgba(255, 80, 80, 0.25);
    --error-text: #ff8080;
  }
}

body {
  background: var(--bg);
  color: var(--text);
  font-family: var(--font-ui);
  font-size: 15px;
  line-height: 1.6;
  min-height: 100vh;
}

@import url('https://fonts.googleapis.com/css2?family=Syne:wght@400;600;700;800&family=DM+Sans:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap');

/* ── UI Elements ── */
.lang-btn {
  background: var(--surface2); border: 1px solid var(--border); color: var(--accent);
  font-family: var(--font-mono); font-size: 0.75rem; font-weight: 600; padding: 0.3rem 0.75rem;
  border-radius: 99px; cursor: pointer; transition: background 0.15s, border-color 0.15s;
}
.lang-btn:hover { background: var(--border); border-color: var(--accent); }

/* ── Page shell ── */
.page-shell { max-width: 1280px; margin: 0 auto; padding: 2.5rem 2rem 4rem; }

/* ── Hero ── */
.hero { 
  background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius);
  padding: 2.5rem; margin-bottom: 2.5rem; position: relative; overflow: hidden; 
}
.hero::before {
  content: ''; position: absolute; inset: 0;
  background: radial-gradient(ellipse 60% 80% at 50% 0%, rgba(79,140,255,0.07) 0%, transparent 70%); pointer-events: none;
}
.hero-lang { position: absolute; top: 1.5rem; right: 1.5rem; z-index: 10; }
.hero h1 {
  font-family: var(--font-hero); font-weight: 800; font-size: clamp(2rem, 5vw, 3.2rem);
  letter-spacing: -0.03em; line-height: 1.1;
  background: linear-gradient(135deg, var(--text) 30%, var(--accent2));
  -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text; margin-bottom: 0.75rem;
  padding-right: 3rem; /* Leave space for lang btn on mobile */
}
.hero p { color: var(--text-dim); max-width: 60ch; font-size: 1rem; position: relative; }
.hero-meta { display: flex; flex-wrap: wrap; gap: 0.6rem; margin-top: 1.5rem; position: relative; }
.meta-chip {
  display: inline-flex; align-items: center; gap: 0.5rem; background: var(--surface2);
  border: 1px solid var(--border); border-radius: 99px; padding: 0.3rem 0.9rem;
  text-decoration: none; color: var(--text); font-size: 0.82rem; transition: border-color 0.15s, background 0.15s;
}
.meta-chip:hover { border-color: var(--accent); background: var(--surface); }
.meta-chip strong { color: var(--accent); font-weight: 600; }
.meta-chip span   { color: var(--text-dim); }
.meta-chip--static { cursor: default; }
.meta-chip--static:hover { border-color: var(--border); background: var(--surface2); }

/* ── Layout grid ── */
.layout-grid { display: grid; grid-template-columns: 320px minmax(0, 1fr); gap: 1.75rem; align-items: start; }

/* ── Panels ── */
.panel { background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius); }
.panel--main { min-width: 0; }
.panel-content { padding: 1.75rem; }
.panel h2 {
  font-family: var(--font-hero); font-size: 1rem; font-weight: 700;
  letter-spacing: 0.03em; text-transform: uppercase; color: var(--text-dim); margin-bottom: 1.25rem;
}

/* ── Selector stack ── */
.selector-stack { display: flex; flex-direction: column; gap: 1.25rem; }
.field-group { display: flex; flex-direction: column; gap: 0.4rem; }
.field-label { font-size: 0.78rem; font-weight: 600; letter-spacing: 0.06em; text-transform: uppercase; color: var(--text-dim); }

/* ── Fuzzy Selector component ── */
.sel-wrap { position: relative; }

.sel-trigger {
  width: 100%; display: flex; align-items: center; justify-content: space-between; gap: 0.5rem;
  padding: 0.65rem 1rem; background: var(--surface2); border: 1px solid var(--border); border-radius: 8px;
  color: var(--text-dim); font-family: var(--font-ui); font-size: 0.95rem; cursor: pointer;
  transition: border-color 0.15s, color 0.15s; text-align: left; user-select: none;
}
.sel-trigger:hover:not(.sel-trigger--disabled) { border-color: var(--accent); }
.sel-trigger--open  { border-color: var(--accent); border-bottom-left-radius: 0; border-bottom-right-radius: 0; }
.sel-trigger--filled { color: var(--text); }
.sel-trigger--disabled { opacity: 0.45; cursor: not-allowed; }

/* Pulse animation for the next required selector */
@keyframes sel-pulse {
  0% { border-color: var(--border); box-shadow: 0 0 0 0 rgba(37, 99, 235, 0); }
  50% { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.15); }
  100% { border-color: var(--border); box-shadow: 0 0 0 0 rgba(37, 99, 235, 0); }
}
@media (prefers-color-scheme: dark) {
  @keyframes sel-pulse {
    0% { border-color: var(--border); box-shadow: 0 0 0 0 rgba(79, 140, 255, 0); }
    50% { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(79, 140, 255, 0.15); }
    100% { border-color: var(--border); box-shadow: 0 0 0 0 rgba(79, 140, 255, 0); }
  }
}
.sel-trigger--pulse { animation: sel-pulse 2s infinite ease-in-out; }

.sel-value { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }

.sel-search-inline {
  flex: 1; width: 100%; background: transparent; border: none; outline: none;
  color: var(--text); font-family: var(--font-ui); font-size: 16px; padding: 0; margin: 0;
}
.sel-search-inline::placeholder { color: var(--text-dim); }

.sel-chevron { flex-shrink: 0; width: 16px; height: 16px; transition: transform 0.2s; }
.sel-chevron--up { transform: rotate(180deg); }

.sel-dropdown {
  position: absolute; top: calc(100% - 1px); left: 0; right: 0; z-index: 200;
  background: var(--surface2); border: 1px solid var(--accent); border-top: none;
  border-bottom-left-radius: 8px; border-bottom-right-radius: 8px; box-shadow: 0 12px 32px rgba(0,0,0,0.25);
  overflow: hidden;
}

.sel-list {
  list-style: none; max-height: 240px; overflow-y: auto; padding: 0.3rem 0;
  scrollbar-width: thin; scrollbar-color: var(--border) transparent;
}
.sel-option {
  display: flex; align-items: center; justify-content: space-between; padding: 0.6rem 1rem; cursor: pointer;
  font-size: 0.95rem; color: var(--text); transition: background 0.1s; gap: 0.5rem;
}
.sel-option:hover, .sel-option--focused { background: var(--border); }
.sel-option--selected { color: var(--accent); }
.sel-option--disabled { opacity: 0.4; cursor: not-allowed; pointer-events: none; }
.sel-option--empty { color: var(--text-dim); font-style: italic; justify-content: center; }
.sel-check { width: 14px; height: 14px; flex-shrink: 0; color: var(--accent); }

/* Alpine transition helpers */
.sel-enter { transition: opacity 0.12s, transform 0.12s; }
.sel-enter-from { opacity: 0; transform: translateY(-4px); }
.sel-enter-to   { opacity: 1; transform: translateY(0); }
.sel-leave { transition: opacity 0.1s; }
.sel-leave-from { opacity: 1; }
.sel-leave-to   { opacity: 0; }

/* ── Sidebar note ── */
.selector-note {
  margin-top: 1.5rem; padding: 0.75rem 1rem; background: var(--surface2);
  border-left: 3px solid var(--accent2); border-radius: 0 6px 6px 0;
  font-size: 0.85rem; color: var(--text-dim); line-height: 1.5;
}
.status-note {
  margin-top: 0.75rem; padding: 0.6rem 0.9rem; background: var(--error-bg);
  border: 1px solid var(--error-border); border-radius: 6px; font-size: 0.8rem; color: var(--error-text);
}

/* ── Instruction panel ── */
.instruction-header { margin-bottom: 1.75rem; }
.instruction-header h2 {
  font-family: var(--font-hero); font-size: 1rem; font-weight: 700; letter-spacing: 0.03em;
  text-transform: uppercase; color: var(--text-dim); margin-bottom: 0.3rem;
}
.instruction-header p { font-size: 0.9rem; color: var(--text-dim); }

/* ── Empty states ── */
.empty-state { 
  text-align: center; padding: 4rem 1.5rem; color: var(--text-dim); 
  border: 1px dashed var(--border); border-radius: var(--radius); 
  background: transparent;
}
.empty-state strong { display: block; font-size: 1.05rem; color: var(--text); margin-bottom: 0.5rem; }
.empty-state p { font-size: 0.95rem; }

/* ── Install card ── */
.install-list { display: block; min-width: 0; }
.install-card { min-width: 0; }
.install-card h3 { font-family: var(--font-hero); font-size: 1.05rem; font-weight: 700; color: var(--text); margin-bottom: 0.4rem; }
.install-card > p { font-size: 0.9rem; color: var(--text-dim); margin-bottom: 1.25rem; }
.command-grid { display: flex; flex-direction: column; gap: 1rem; min-width: 0; }

/* ── Command card ── */
.command-card { background: var(--code-bg); border: 1px solid var(--border); border-radius: var(--radius); overflow: hidden; min-width: 0; max-width: 100%; }
.command-card header { display: flex; align-items: center; justify-content: space-between; padding: 0.6rem 1rem; background: var(--surface2); border-bottom: 1px solid var(--border); }
.command-card header strong { font-size: 0.85rem; font-weight: 600; color: var(--text-dim); letter-spacing: 0.03em; }
.copy-btn {
  background: transparent; border: 1px solid var(--border); color: var(--accent); font-family: var(--font-mono); font-size: 0.75rem; font-weight: 600;
  letter-spacing: 0.06em; padding: 0.25rem 0.7rem; border-radius: 99px; cursor: pointer; transition: background 0.15s, border-color 0.15s;
}
.copy-btn:hover { background: rgba(79,140,255,0.1); border-color: var(--accent); }

.command-card pre { padding: 1.25rem; overflow-x: auto; scrollbar-width: thin; scrollbar-color: var(--border) transparent; }
.command-card code { font-family: var(--font-mono); font-size: 0.875rem; line-height: 1.8; color: var(--green); white-space: pre; }

/* ── Responsive Mobile ── */
@media (max-width: 860px) {
  .layout-grid { 
    grid-template-columns: minmax(250px, 1fr); 
    width: 100%;
    max-width: 100%;
  }
}

@media (max-width: 600px) {
  .page-shell { padding: 1.5rem 1rem 2rem; }
  .hero { padding: 1.75rem 1.25rem; margin-bottom: 1.5rem; }
  .hero-lang { top: 1.25rem; right: 1.25rem; }
  .hero h1 { font-size: 2rem; }
  .panel-content { padding: 1.25rem; }
  .sel-trigger { padding: 0.8rem 1rem; }
  .sel-option { padding: 0.8rem 1rem; }
}

/* ── Scrollbar (webkit) ── */
::-webkit-scrollbar { width: 6px; height: 6px; }
::-webkit-scrollbar-track { background: transparent; }
::-webkit-scrollbar-thumb { background: var(--border); border-radius: 3px; }
  `;

  window.renderRepositoryInstructions = renderRepositoryInstructions;
})();
