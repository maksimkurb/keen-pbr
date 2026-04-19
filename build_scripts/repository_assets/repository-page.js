(function() {
  "use strict";

  var SYSTEMS = [
    {
      id: "keenetic",
      label: "Keenetic / Netcraze",
      name: "Keenetic",
      description: "Entware opkg feed for Keenetic and Netcraze routers.",
      catalogKey: "keenetic"
    },
    {
      id: "openwrtOpkg",
      label: "OpenWrt 24.x and lower",
      name: "OpenWrt",
      description: "Classic opkg/ipk feed for older OpenWrt releases.",
      catalogKey: "openwrtOpkg"
    },
    {
      id: "openwrtApk",
      label: "OpenWrt 25.x+",
      name: "OpenWrt",
      description: "APK repository for newer OpenWrt releases.",
      catalogKey: "openwrtApk"
    },
    {
      id: "debian",
      label: "Debian",
      name: "Debian",
      description: "APT repository for Debian systems.",
      catalogKey: "debian"
    }
  ];

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/\"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function getSystemDefinition(systemId) {
    return SYSTEMS.find(function(system) {
      return system.id === systemId;
    }) || SYSTEMS[0];
  }

  function getEntries(config, systemId) {
    var system = getSystemDefinition(systemId);
    var catalog = config.catalog || {};
    var entries = catalog[system.catalogKey];
    return Array.isArray(entries) ? entries.slice() : [];
  }

  function uniqueVersions(entries) {
    var values = Array.from(
      new Set(
        entries.map(function(entry) {
          return entry.version;
        })
      )
    );
    values.sort();
    return values;
  }

  function uniqueArchitectures(entries, version) {
    var values = Array.from(
      new Set(
        entries
          .filter(function(entry) {
            return entry.version === version;
          })
          .map(function(entry) {
            return entry.arch;
          })
      )
    );
    values.sort();
    return values;
  }

  function pickDefaultSystem(config) {
    var firstWithEntries = SYSTEMS.find(function(system) {
      return getEntries(config, system.id).length > 0;
    });
    return firstWithEntries ? firstWithEntries.id : SYSTEMS[0].id;
  }

  function buildPlaceholderOption(label, selected) {
    return '<option value="" disabled="disabled"' + (selected ? ' selected="selected"' : "") + ">" + escapeHtml(label) + "</option>";
  }

  function copyText(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      return navigator.clipboard.writeText(text);
    }

    return new Promise(function(resolve, reject) {
      var textarea = document.createElement("textarea");
      textarea.value = text;
      textarea.setAttribute("readonly", "readonly");
      textarea.style.position = "absolute";
      textarea.style.left = "-9999px";
      document.body.appendChild(textarea);
      textarea.select();
      if (document.execCommand("copy")) {
        document.body.removeChild(textarea);
        resolve();
        return;
      }
      document.body.removeChild(textarea);
      reject(new Error("Clipboard API unavailable"));
    });
  }

  function renderMetaChips(config) {
    var chips = [
      '<a class="meta-chip" href="' +
        escapeHtml(config.baseUrl + "/") +
        '">' +
        "<strong>Repository</strong>" +
        '<span>' + escapeHtml(config.baseUrl) + "</span>" +
      "</a>"
    ];

    var source = config.source || {};
    if (source.refUrl && source.refLabel) {
      chips.push(
        '<a class="meta-chip" href="' +
          escapeHtml(source.refUrl) +
          '">' +
          "<strong>Source</strong>" +
          '<span>' + escapeHtml(source.refLabel) + "</span>" +
        "</a>"
      );
    }
    if (source.prUrl && source.prNumber) {
      chips.push(
        '<a class="meta-chip" href="' +
          escapeHtml(source.prUrl) +
          '">' +
          "<strong>Pull Request</strong>" +
          '<span>#' + escapeHtml(source.prNumber) + "</span>" +
        "</a>"
      );
    }

    return chips.join("");
  }

  function keyUrlForSystem(systemId, keysManifest) {
    if (!keysManifest) {
      return "";
    }
    if (systemId === "openwrtOpkg") {
      return keysManifest.openwrt_opkg && keysManifest.openwrt_opkg.key || "";
    }
    if (systemId === "openwrtApk") {
      return keysManifest.openwrt_apk && keysManifest.openwrt_apk.key || "";
    }
    if (systemId === "debian") {
      return keysManifest.debian && keysManifest.debian.key || "";
    }
    return "";
  }

  function keyBlockForSystem(systemId, keysManifest, keysError) {
    if (systemId === "keenetic") {
      return "# No signing key installation is required for Keenetic / Netcraze feeds";
    }

    if (keysError) {
      return "# Failed to load /keys/keys.json\n# " + keysError;
    }

    var keyUrl = keyUrlForSystem(systemId, keysManifest);
    if (!keyUrl) {
      return "# Loading signing key information from /keys/keys.json";
    }

    if (systemId === "openwrtOpkg") {
      return [
        "wget " + keyUrl + " -O /tmp/openwrt_opkg_public.key",
        "opkg-key add /tmp/openwrt_opkg_public.key"
      ].join("\n");
    }

    if (systemId === "openwrtApk") {
      return [
        "wget " + keyUrl + " -O /etc/apk/keys/openwrt_apk_public.pem",
        "grep -qxF '/etc/apk/keys/openwrt_apk_public.pem' /etc/sysupgrade.conf || echo '/etc/apk/keys/openwrt_apk_public.pem' >> /etc/sysupgrade.conf"
      ].join("\n");
    }

    return [
      "wget " + keyUrl + " -O /usr/share/keyrings/keen-pbr-archive-keyring.asc",
      "chmod 0644 /usr/share/keyrings/keen-pbr-archive-keyring.asc"
    ].join("\n");
  }

  function addRepositoryBlock(systemId, entry) {
    if (systemId === "keenetic") {
      return "printf '%s\\n' '" + entry.feedLine + "' > /opt/etc/opkg/keen-pbr.conf";
    }
    if (systemId === "openwrtOpkg") {
      return "printf '%s\\n' '" + entry.feedLine + "' > /etc/opkg/keen-pbr.conf";
    }
    if (systemId === "openwrtApk") {
      return [
        "mkdir -p /etc/apk/repositories.d",
        "printf '%s\\n' '" + entry.repositoryUrl + "' > /etc/apk/repositories.d/keen-pbr.list"
      ].join("\n");
    }
    return "printf '%s\\n' '" + entry.sourceLine + "' > /etc/apt/sources.list.d/keen-pbr.list";
  }

  function installBlock(systemId) {
    if (systemId === "debian") {
      return "apt update\napt install keen-pbr";
    }
    if (systemId === "openwrtApk") {
      return "apk update\napk add keen-pbr";
    }
    return "opkg update\nopkg install keen-pbr";
  }

  function preRepositoryBlock(systemId) {
    if (systemId === "keenetic") {
      return "opkg update\nopkg install wget-ssl";
    }
    return "";
  }

  function commandCard(title, code) {
    return [
      '<section class="command-card">',
      "<header>",
      "<strong>" + escapeHtml(title) + "</strong>",
      '<button class="copy-button" type="button">Copy</button>',
      "</header>",
      "<pre><code>" + escapeHtml(code) + "</code></pre>",
      "</section>"
    ].join("");
  }

  function renderInstallCard(systemId, entry, keysManifest, keysError) {
    var cards = [];
    var needsPreRepositoryBlock = systemId === "keenetic";

    if (systemId !== "keenetic") {
      cards.push(
        commandCard("1. Install signing keys", keyBlockForSystem(systemId, keysManifest, keysError))
      );
    }

    if (needsPreRepositoryBlock) {
      cards.push(
        commandCard("1. Install wget-ssl for HTTPS support", preRepositoryBlock(systemId))
      );
    }

    cards.push(
      commandCard(
        "2. Add repository / feed",
        addRepositoryBlock(systemId, entry)
      )
    );
    cards.push(
      commandCard(
        "3. Update and install keen-pbr",
        installBlock(systemId)
      )
    );

    return [
      '<article class="install-card">',
      "<h3>OS: " + escapeHtml(SYSTEMS.find(function(s) { return s.id === systemId; }).name) + "; version: " + escapeHtml(entry.version) + "; architecture: " + escapeHtml(entry.arch) + "</h3>",
      "<p>Use these commands to trust the repository, register the feed, and install <code>keen-pbr</code>.</p>",
      '<div class="command-grid">' + cards.join("") + "</div>",
      "</article>"
    ].join("");
  }

  function renderSelectionPrompt(title, message) {
    return [
      '<div style="position: relative; padding-top: 3.5rem;">',
      '<svg aria-hidden="true" viewBox="0 0 800 800" width="220" height="160" style="position: absolute; top: -3rem; left: -7rem; overflow: visible; pointer-events: none;">',
      '<g stroke-width="18" stroke="currentColor" fill="none" stroke-linecap="round" stroke-linejoin="round" opacity="0.28" transform="matrix(-0.9781476007338057,0.20791169081775931,-0.20791169081775931,-0.9781476007338057,869.423716620626,708.0943639664185)">',
      '<path d="M253.40797424316406 247.5Q180.40797424316406 474.5 558.4079742431641 552.5" marker-end="url(#selectionPromptArrowhead)"></path>',
      "</g>",
      "<defs>",
      '<marker markerWidth="3.5" markerHeight="3.5" refX="1.75" refY="1.75" viewBox="0 0 3.5 3.5" orient="auto" id="selectionPromptArrowhead">',
      '<polygon points="0,3.5 1.1666666666666667,1.75 0,0 3.5,1.75" fill="currentColor"></polygon>',
      "</marker>",
      "</defs>",
      "</svg>",
      '<div class="empty-state">',
      "<strong>" + escapeHtml(title) + "</strong>",
      "<p>" + escapeHtml(message) + "</p>",
      "</div>",
      "</div>"
    ].join("");
  }

  function renderInstructions(state, systemId, entry, keysManifest, keysError) {
    if (!state.selectedSystem) {
      return renderSelectionPrompt(
        "Select OS",
        "Choose an operating system on the left to generate installation commands."
      );
    }

    if (!state.selectedVersion || !state.selectedArch) {
      return renderSelectionPrompt(
        "Select options",
        "Finish choosing the version and architecture on the left to see the exact commands."
      );
    }

    if (!entry) {
      return '<div class="empty-state">No packages are currently published for this selection.</div>';
    }

    return renderInstallCard(systemId, entry, keysManifest, keysError);
  }

  function attachInteractions(root, state) {
    var systemSelect = root.querySelector("[data-system-select]");
    var archSelect = root.querySelector("[data-arch-select]");
    var versionSelect = root.querySelector("[data-version-select]");
    var buttons = root.querySelectorAll(".copy-button");

    if (systemSelect) {
      systemSelect.addEventListener("change", function(event) {
        state.selectedSystem = event.target.value;
        state.selectedVersion = "";
        state.selectedArch = "";
        state.render();
      });
    }

    if (versionSelect) {
      versionSelect.addEventListener("change", function(event) {
        state.selectedVersion = event.target.value;
        state.selectedArch = "";
        state.render();
      });
    }

    if (archSelect) {
      archSelect.addEventListener("change", function(event) {
        state.selectedArch = event.target.value;
        state.render();
      });
    }

    buttons.forEach(function(button) {
      button.addEventListener("click", function() {
        var code = button.closest(".command-card").querySelector("code").textContent;
        copyText(code).then(function() {
          var original = button.textContent;
          button.textContent = "Copied";
          window.setTimeout(function() {
            button.textContent = original;
          }, 1400);
        }).catch(function() {
          button.textContent = "Failed";
          window.setTimeout(function() {
            button.textContent = "Copy";
          }, 1400);
        });
      });
    });
  }

  function renderApp(root, state, config) {
    var selectedSystem = state.selectedSystem ? getSystemDefinition(state.selectedSystem) : null;
    var systemEntries = selectedSystem ? getEntries(config, selectedSystem.id) : [];
    var versions = selectedSystem ? uniqueVersions(systemEntries) : [];

    if (!versions.includes(state.selectedVersion)) {
      state.selectedVersion = versions.length === 1 ? versions[0] : "";
    }

    var arches = state.selectedVersion ? uniqueArchitectures(systemEntries, state.selectedVersion) : [];
    if (!arches.includes(state.selectedArch)) {
      state.selectedArch = arches.length === 1 ? arches[0] : "";
    }

    var selectedEntry =
      systemEntries.find(function(entry) {
        return entry.version === state.selectedVersion && entry.arch === state.selectedArch;
      }) || null;

    var keysNote = "";
    if (state.keysError) {
      keysNote = '<div class="status-note">' + escapeHtml(state.keysError) + "</div>";
    }

    var systemOptions = [buildPlaceholderOption("Select OS...", !state.selectedSystem)].concat(SYSTEMS.map(function(system) {
      var selected = selectedSystem && system.id === selectedSystem.id ? ' selected="selected"' : "";
      var disabled = getEntries(config, system.id).length ? "" : ' disabled="disabled"';
      return '<option value="' + escapeHtml(system.id) + '"' + selected + disabled + ">" + escapeHtml(system.label) + "</option>";
    })).join("");

    var archOptions = [buildPlaceholderOption(
      !state.selectedSystem ? "Select OS first..." : !state.selectedVersion ? "Select version first..." : "Select architecture...",
      !state.selectedArch
    )].concat(arches.map(function(arch) {
      var selected = arch === state.selectedArch ? ' selected="selected"' : "";
      return '<option value="' + escapeHtml(arch) + '"' + selected + ">" + escapeHtml(arch) + "</option>";
    })).join("");
    var versionOptions = [buildPlaceholderOption(
      state.selectedSystem ? "Select version..." : "Select OS first...",
      !state.selectedVersion
    )].concat(versions.map(function(version) {
      var selected = version === state.selectedVersion ? ' selected="selected"' : "";
      return '<option value="' + escapeHtml(version) + '"' + selected + ">" + escapeHtml(version) + "</option>";
    })).join("");

    root.innerHTML = [
      '<div class="page-shell">',
      '<section class="hero">',
      '<h1>keen-pbr repository</h1>',
      '<p>Select your OS family and architecture to generate the exact commands needed to add this repository, and install <code>keen-pbr</code>.</p>',
      '<div class="hero-meta">' + renderMetaChips(config) + "</div>",
      "</section>",
      '<div class="layout-grid">',
      '<aside class="panel"><div class="panel-content">',
      "<h2>Choose your target</h2>",
      '<div class="selector-stack">',
      "<div>",
      '<label class="field-label" for="repository-system">Select OS</label>',
      '<div class="select-wrap"><select id="repository-system" data-system-select>' + systemOptions + "</select></div>",
      "</div>",
      "<div>",
      '<label class="field-label" for="repository-version">Select version</label>',
      '<div class="select-wrap"><select id="repository-version" data-version-select' + (state.selectedSystem ? "" : ' disabled="disabled"') + ">" + versionOptions + "</select></div>",
      "</div>",
      "<div>",
      '<label class="field-label" for="repository-arch">Select architecture</label>',
      '<div class="select-wrap"><select id="repository-arch" data-arch-select' + (state.selectedVersion ? "" : ' disabled="disabled"') + ">" + archOptions + "</select></div>",
      "</div>",
      "</div>",
      '<div class="selector-note">' + (
        selectedSystem
          ? "<strong>" + escapeHtml(selectedSystem.label) + "</strong>" + escapeHtml(selectedSystem.description)
          : "Pick an operating system to unlock the matching version and architecture options."
      ) + "</div>",
      keysNote,
      "</div></aside>",
      '<section class="panel"><div class="panel-content">',
      '<div class="instruction-header">',
      "<div>",
      "<h2>Install instructions</h2>",
      "<p>Commands update immediately when you change the operating system, version, or architecture selector.</p>",
      "</div>",
      "</div>",
      '<div class="install-list">' + renderInstructions(state, selectedSystem ? selectedSystem.id : "", selectedEntry, state.keysManifest, state.keysError) + "</div>",
      "</div></section>",
      "</div>",
      "</div>"
    ].join("");

    attachInteractions(root, state);
  }

  function renderRepositoryInstructions(config) {
    var root = document.getElementById("app");
    if (!root) {
      return;
    }

    var state = {
      selectedSystem: "",
      selectedArch: "",
      selectedVersion: "",
      keysManifest: null,
      keysError: "",
      render: function() {
        renderApp(root, state, config);
      }
    };

    state.render();

    fetch("/keys/keys.json", { cache: "no-store" })
      .then(function(response) {
        if (!response.ok) {
          throw new Error("Unable to fetch /keys/keys.json");
        }
        return response.json();
      })
      .then(function(manifest) {
        state.keysManifest = manifest;
        state.keysError = "";
        state.render();
      })
      .catch(function(error) {
        state.keysManifest = null;
        state.keysError = error && error.message ? error.message : "Unable to load signing key metadata.";
        state.render();
      });
  }

  window.renderRepositoryInstructions = renderRepositoryInstructions;
})();
