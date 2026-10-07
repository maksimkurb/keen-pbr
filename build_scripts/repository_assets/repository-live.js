(function () {
  "use strict";

  function rank(item) {
    return [
      Number(item.generation || 0),
      Number(item.run_id || 0),
      Number(item.run_attempt || 0),
      Number(item.published_at || 0),
    ];
  }

  function isNewerOrEqual(a, b) {
    for (var i = 0; i < a.length; i += 1) {
      if (a[i] !== b[i]) return a[i] > b[i];
    }
    return true;
  }

  function latestRecords(text) {
    var latest = new Map();
    text.split("\n").forEach(function (line) {
      line = line.trim();
      if (!line) return;
      try {
        var item = JSON.parse(line);
        if (!item.key) return;
        var previous = latest.get(item.key);
        if (!previous || isNewerOrEqual(rank(item), rank(previous))) {
          latest.set(item.key, item);
        }
      } catch (error) {
        console.warn("Ignoring malformed ARTIFACTS_WAL row", error);
      }
    });
    return Array.from(latest.values());
  }

  function buildCatalog(baseUrl, records) {
    var catalog = { keenetic: [], openwrtOpkg: [], openwrtApk: [], debian: [], ubuntu: [] };
    records.forEach(function (item) {
      var version = item.version;
      var arch = item.arch;
      var formats = Array.isArray(item.formats) ? item.formats : [];
      var rel = [item.platform, version, arch].map(encodeURIComponent).join("/");
      var url = baseUrl.replace(/\/$/, "") + "/" + rel;

      if (item.platform === "openwrt") {
        if (formats.indexOf("opkg") !== -1) {
          catalog.openwrtOpkg.push({ version: version, arch: arch, feedLine: "src/gz keen-pbr " + url });
        }
        if (formats.indexOf("apk") !== -1) {
          catalog.openwrtApk.push({ version: version, arch: arch, repositoryUrl: url + "/packages.adb" });
        }
      } else if (item.platform === "keenetic") {
        catalog.keenetic.push({ version: version, arch: arch, feedLine: "src/gz keen-pbr " + url });
      } else if (item.platform === "debian") {
        var entry = {
          version: version,
          arch: arch,
          sourceLine: "deb [signed-by=/usr/share/keyrings/keen-pbr-archive-keyring.asc] " + url + " ./",
        };
        catalog.debian.push(entry);
        if (version === "bookworm") {
          catalog.ubuntu.push({ version: "noble", arch: arch, sourceLine: entry.sourceLine });
        } else if (version === "trixie") {
          catalog.ubuntu.push({ version: "resolute", arch: arch, sourceLine: entry.sourceLine });
        }
      }
    });

    Object.keys(catalog).forEach(function (key) {
      catalog[key].sort(function (a, b) {
        return (a.version + "\u0000" + a.arch).localeCompare(b.version + "\u0000" + b.arch);
      });
    });
    return catalog;
  }

  function showLoadError(error) {
    var root = document.getElementById("app");
    if (!root) return;
    root.innerHTML =
      '<main style="max-width:760px;margin:4rem auto;padding:0 1rem;font-family:system-ui,sans-serif">' +
      "<h1>keen-pbr repository</h1><p>Unable to load the published artifact catalog.</p>" +
      '<pre style="white-space:pre-wrap">' + String(error && error.message ? error.message : error) + "</pre></main>";
  }

  Promise.all([
    fetch("./.release.json", { cache: "no-store" }).then(function (response) {
      if (!response.ok) throw new Error("Unable to fetch .release.json");
      return response.json();
    }),
    fetch("./ARTIFACTS_WAL.jsonl", { cache: "no-store" }).then(function (response) {
      if (!response.ok) throw new Error("Unable to fetch ARTIFACTS_WAL.jsonl");
      return response.text();
    }),
  ])
    .then(function (values) {
      var release = values[0];
      window.renderRepositoryInstructions({
        baseUrl: release.baseUrl,
        targetRoot: release.targetRoot,
        source: release.source || {},
        catalog: buildCatalog(release.baseUrl, latestRecords(values[1])),
      });
    })
    .catch(showLoadError);
})();
