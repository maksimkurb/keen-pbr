#!/usr/bin/env python3

import argparse
import json
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


def fetch_text(url):
    try:
        with urllib.request.urlopen(url) as response:
            return response.read().decode("utf-8", errors="replace")
    except urllib.error.URLError as err:
        raise SystemExit(f"Failed to fetch {url}: {err}") from err


def discover_architectures(version):
    version_url = f"https://downloads.openwrt.org/releases/{version}/targets/"
    index_data = fetch_text(version_url)
    architectures = set()

    for target in parse_links(index_data):
        target_data = fetch_text(f"{version_url}{target}/")
        for subtarget in parse_links(target_data):
            profiles_url = f"{version_url}{target}/{subtarget}/profiles.json"
            try:
                profiles = json.loads(fetch_text(profiles_url))
            except SystemExit:
                continue
            except json.JSONDecodeError:
                continue

            arch = profiles.get("arch_packages")
            if arch:
                architectures.add(arch)

    if not architectures:
        raise SystemExit(f"No architectures discovered for OpenWrt {version}")

    return [
        {
            "openwrt_version": version,
            "architecture": arch,
        }
        for arch in sorted(architectures)
    ]


def emit_table(rows):
    arch_w = max(len("ARCHITECTURE"), max(len(row["architecture"]) for row in rows))
    print(f"{'OPENWRT_VERSION':<15}  {'ARCHITECTURE':<{arch_w}}")
    print(f"{'-' * 15}  {'-' * arch_w}")
    for row in rows:
        print(f"{row['openwrt_version']:<15}  {row['architecture']:<{arch_w}}")


def emit_yaml(rows):
    for row in rows:
        print(f'  - openwrt_version: "{row["openwrt_version"]}"')
        print(f'    architecture: "{row["architecture"]}"')


def parse_args():
    parser = argparse.ArgumentParser(
        description="Discover unique OpenWrt package architectures for a release."
    )
    parser.add_argument("version", help="OpenWrt release version, e.g. 25.12.2")
    parser.add_argument(
        "--format",
        choices=("yaml", "json", "table"),
        default="yaml",
        help="Output format (default: yaml).",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    rows = discover_architectures(args.version)

    if args.format == "json":
        print(json.dumps(rows, indent=2))
        return 0
    if args.format == "table":
        emit_table(rows)
        return 0

    emit_yaml(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
