#!/usr/bin/env python3

import json
import re
from pathlib import Path

workflow = Path(".github/workflows/release-packages.yml").read_text(encoding="utf-8")
match = re.search(
    r"(?ms)^  openwrt:\n.*?^        build:\n(?P<body>.*?)(?=^    uses: \./\.github/workflows/reusable-openwrt-packages\.yml)",
    workflow,
)
if not match:
    raise SystemExit("Unable to locate the release OpenWrt matrix")

rows = []
version = None
for line in match.group("body").splitlines():
    version_match = re.match(r'\s*-\s+openwrt_version:\s+"([^"]+)"\s*$', line)
    if version_match:
        version = version_match.group(1)
        continue
    arch_match = re.match(r'\s+architecture:\s+"([^"]+)"\s*$', line)
    if arch_match and version:
        rows.append({"openwrt_version": version, "architecture": arch_match.group(1)})
        version = None

if not rows:
    raise SystemExit("Release OpenWrt matrix is empty")
print(json.dumps(rows, separators=(",", ":")))
