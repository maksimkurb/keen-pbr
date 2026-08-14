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
      stepInstallWget: "1. Install HTTPS and CA certificate support",
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
      stepInstallWget: "1. Установить поддержку HTTPS и CA-сертификатов",
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
    if (systemId === "keenetic") {
      return[
        "mkdir -p /opt/etc/opkg",
        "printf '%s\\n' '" + entry.feedLine + "' > /opt/etc/opkg/keen-pbr.conf",
      ].join("\n");
    }
    if (systemId === "openwrtOpkg") {
      return[
        "mkdir -p /etc/opkg",
        "printf '%s\\n' '" + entry.feedLine + "' > /etc/opkg/keen-pbr.conf",
      ].join("\n");
    }
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
    return "opkg update\nopkg install wget-ssl ca-bundle ca-certificates";
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
        var copyText = code.endsWith("\n") ? code : code + "\n";
        function flash(label) {
          btn.textContent = label;
          setTimeout(function () { btn.textContent = self.t("copy"); }, 1400);
        }
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(copyText)
            .then(function () { flash(self.t("copied")); })
            .catch(function () { flash(self.t("copyFailed")); });
        } else {
          var ta = document.createElement("textarea");
          ta.value = copyText;
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
    <div class="brand-lockup">
      <img class="brand-logo" src="/assets/logo.svg" alt="keen-pbr">
      <h1 x-text="t('pageTitle')"></h1>
    </div>
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

  window.renderRepositoryInstructions = renderRepositoryInstructions;
})();
