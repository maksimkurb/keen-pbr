#!/usr/bin/env python3

import json
import os
import re
import sys
import urllib.error
import urllib.request
from html.parser import HTMLParser


class LinkParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs = []

    def handle_starttag(self, tag, attrs):
        if tag != "a":
            return
        for key, value in attrs:
            if key == "href" and value:
                self.hrefs.append(value)
                break


def parse_links(html):
    parser = LinkParser()
    parser.feed(html)
    return [
        href.rstrip("/")
        for href in parser.hrefs
        if href.endswith("/")
        and not href.startswith("/")
        and "://" not in href
        and href != "../"
    ]


def fetch_safe(url):
    try:
        with urllib.request.urlopen(url) as response:
            return response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as err:
        print(f"SKIP {url} (HTTP {err.code})", file=sys.stderr)
        return None
    except urllib.error.URLError as err:
        print(f"SKIP {url} ({err.reason})", file=sys.stderr)
        return None


def set_output(name, value):
    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a", encoding="utf-8") as fh:
            fh.write(f"{name}={value}\n")
    else:
        print(f"{name}={value}")


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "24.10.4"
    filter_targets = [item for item in (sys.argv[2] if len(sys.argv) > 2 else "").split(",") if item]
    filter_subtargets = [item for item in (sys.argv[3] if len(sys.argv) > 3 else "").split(",") if item]

    version_url = f"https://downloads.openwrt.org/releases/{version}/targets/"
    index_data = fetch_safe(version_url)
    if not index_data:
        print(f"OpenWrt release not found: {version_url}", file=sys.stderr)
        sys.exit(1)

    builds = []

    for target in parse_links(index_data):
        if filter_targets and target not in filter_targets:
            continue

        target_data = fetch_safe(f"{version_url}{target}/")
        if not target_data:
            continue

        for subtarget in parse_links(target_data):
            if filter_subtargets and subtarget not in filter_subtargets:
                continue

            subtarget_url = f"{version_url}{target}/{subtarget}/"
            subtarget_data = fetch_safe(subtarget_url)
            if not subtarget_data:
                continue

            hrefs = LinkParser()
            hrefs.feed(subtarget_data)
            href_list = hrefs.hrefs

            sdk_file = next((href for href in href_list if re.match(r"openwrt-sdk-.*\.tar\.zst$", href)), None)
            if not sdk_file:
                continue

            pkgarch = ""
            manifest_file = next((href for href in href_list if re.match(r"Packages\.manifest$", href)), None)
            if manifest_file:
                manifest = fetch_safe(f"{subtarget_url}{manifest_file}")
                if manifest:
                    match = re.search(r"Architecture: (\S+)", manifest)
                    if match:
                        pkgarch = match.group(1)

            builds.append(
                {
                    "tag": version,
                    "target": target,
                    "subtarget": subtarget,
                    "pkgarch": pkgarch,
                    "sdk_file": sdk_file,
                }
            )

    set_output("job-config", json.dumps(builds))
    print(f"Generated {len(builds)} build targets")


if __name__ == "__main__":
    main()
