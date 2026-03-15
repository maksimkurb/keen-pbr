const core = require("@actions/core");
const axios = require("axios");
const cheerio = require("cheerio");

async function generateMatrix(version, filterTargets, filterSubtargets) {
  const versionURL = `https://downloads.openwrt.org/releases/${version}/targets/`;

  try {
    const $ = cheerio.load((await axios.get(versionURL)).data);

    const allTargets = $('a[href$="/"]')
      .map((i, el) => $(el).attr("href"))
      .get()
      .filter((h) => h !== "../")
      .map((h) => h.replace("/", ""));

    const builds = [];

    for (const target of allTargets) {
      const $t = cheerio.load(
        (await axios.get(`${versionURL}${target}/`)).data
      );

      const subtargets = $t('a[href$="/"]')
        .map((i, el) => $t(el).attr("href"))
        .get()
        .filter((h) => h !== "../")
        .map((h) => h.replace("/", ""));

      for (const subtarget of subtargets) {
        // Apply optional filters (for workflow_dispatch)
        if (filterTargets.length && !filterTargets.includes(target)) continue;
        if (filterSubtargets.length && !filterSubtargets.includes(subtarget))
          continue;

        const subtargetURL = `${versionURL}${target}/${subtarget}/`;
        const $st = cheerio.load((await axios.get(subtargetURL)).data);
        const hrefs = $st("a")
          .map((i, el) => $st(el).attr("href"))
          .get();

        // Skip if no SDK available for this target
        const sdkFile = hrefs.find((h) => h?.match(/openwrt-sdk-.*\.tar\.zst$/));
        if (!sdkFile) continue;

        // Get pkgarch from Packages.manifest
        const manifestFile = hrefs.find((h) => h?.match(/Packages\.manifest/));
        let pkgarch = "";
        if (manifestFile) {
          const manifest = (await axios.get(`${subtargetURL}${manifestFile}`))
            .data;
          const m = manifest.match(/Architecture: (\S+)/);
          pkgarch = m ? m[1] : "";
        }

        builds.push({ tag: version, target, subtarget, pkgarch, sdk_file: sdkFile });
      }
    }

    core.setOutput("job-config", JSON.stringify(builds));
    console.log(`Generated ${builds.length} build targets`);
  } catch (err) {
    core.setFailed(`Failed: ${err}`);
  }
}

const version = process.argv[2] || "24.10.4";
const targets = process.argv[3]?.split(",").filter(Boolean) || [];
const subtargets = process.argv[4]?.split(",").filter(Boolean) || [];
generateMatrix(version, targets, subtargets);
