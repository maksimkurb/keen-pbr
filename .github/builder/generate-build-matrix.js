const core = require("@actions/core");
const axios = require("axios");
const cheerio = require("cheerio");

// Extract directory names from an Apache-style index page.
// Keeps only relative single-segment hrefs (e.g. "ath79/"), drops "../" and absolute paths.
function parseLinks($) {
  return $('a[href$="/"]')
    .map((i, el) => $(el).attr("href"))
    .get()
    .filter((h) => h && !h.startsWith("/") && !h.includes("://") && h !== "../")
    .map((h) => h.replace(/\/$/, ""));
}

// Fetch a URL and return the response data, or null on non-2xx / network error.
async function fetchSafe(url) {
  try {
    const res = await axios.get(url, { validateStatus: () => true });
    if (res.status < 200 || res.status >= 300) {
      console.warn(`SKIP ${url} (HTTP ${res.status})`);
      return null;
    }
    return res.data;
  } catch (err) {
    console.warn(`SKIP ${url} (${err.message})`);
    return null;
  }
}

async function generateMatrix(version, filterTargets, filterSubtargets) {
  const versionURL = `https://downloads.openwrt.org/releases/${version}/targets/`;

  const indexData = await fetchSafe(versionURL);
  if (!indexData) {
    core.setFailed(`OpenWrt release not found: ${versionURL}`);
    return;
  }

  const $ = cheerio.load(indexData);
  const allTargets = parseLinks($);

  const builds = [];

  for (const target of allTargets) {
    // Apply target filter early to avoid unnecessary requests
    if (filterTargets.length && !filterTargets.includes(target)) continue;

    const targetData = await fetchSafe(`${versionURL}${target}/`);
    if (!targetData) continue;

    const $t = cheerio.load(targetData);
    const subtargets = parseLinks($t);

    for (const subtarget of subtargets) {
      if (filterSubtargets.length && !filterSubtargets.includes(subtarget)) continue;

      const subtargetURL = `${versionURL}${target}/${subtarget}/`;
      const subtargetData = await fetchSafe(subtargetURL);
      if (!subtargetData) continue;

      const $st = cheerio.load(subtargetData);
      const hrefs = $st("a").map((i, el) => $st(el).attr("href")).get();

      // Skip if no SDK available for this target
      const sdkFile = hrefs.find((h) => h?.match(/openwrt-sdk-.*\.tar\.zst$/));
      if (!sdkFile) continue;

      // Get pkgarch from Packages.manifest
      let pkgarch = "";
      const manifestFile = hrefs.find((h) => h?.match(/Packages\.manifest/));
      if (manifestFile) {
        const manifest = await fetchSafe(`${subtargetURL}${manifestFile}`);
        if (manifest) {
          const m = manifest.match(/Architecture: (\S+)/);
          pkgarch = m ? m[1] : "";
        }
      }

      builds.push({ tag: version, target, subtarget, pkgarch, sdk_file: sdkFile });
    }
  }

  core.setOutput("job-config", JSON.stringify(builds));
  console.log(`Generated ${builds.length} build targets`);
}

const version = process.argv[2] || "24.10.4";
const targets = process.argv[3]?.split(",").filter(Boolean) || [];
const subtargets = process.argv[4]?.split(",").filter(Boolean) || [];
generateMatrix(version, targets, subtargets);
