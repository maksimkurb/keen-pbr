#!/usr/bin/env python3
"""Dependency guard: runtime, backends, health and the daemon must not know
concrete firewall policy modules.

Policy modules live in src/firewall/rules/. Everything else reaches them only
through the shared manifest/context header (firewall_rule_modules.hpp) and the
plan model (firewall_plan.hpp, firewall_rule.hpp). This script fails when a
guarded file includes a rules/ file or spells a module id; module ids are read
from the kModuleId constants in src/firewall/rules/*.cpp, so adding a module
extends the guard automatically.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
RULES = SRC / "firewall" / "rules"

GUARDED_GLOBS = [
    "firewall/firewall_runtime.*",
    "firewall/firewall_lowering.*",
    "firewall/firewall_plan_verifier.*",
    "firewall/firewall_snapshot.*",
    "firewall/firewall_physical.*",
    "firewall/firewall_verifier.*",
    "firewall/iptables.*",
    "firewall/nftables.*",
    "health/**/*",
    "daemon/**/*",
]
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc"}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
MODULE_ID_RE = re.compile(r'kModuleId\s*=\s*"([^"]+)"')
# Prefilter ids are also matched by prefix so a typo'd or new id cannot slip in.
ID_PREFIXES = ("prefilter.",)


def module_ids():
    ids = set()
    for path in sorted(RULES.glob("*.cpp")):
        ids.update(MODULE_ID_RE.findall(path.read_text()))
    return ids


def guarded_files():
    files = set()
    for pattern in GUARDED_GLOBS:
        for path in SRC.glob(pattern):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                files.add(path)
    return sorted(files)


def main():
    ids = module_ids()
    if len(ids) < 9:
        print(f"firewall dependency guard: found only {len(ids)} module ids "
              f"in {RULES}; the guard is misconfigured", file=sys.stderr)
        return 2
    files = guarded_files()
    if not files:
        print("firewall dependency guard: no guarded files found", file=sys.stderr)
        return 2

    problems = []
    for path in files:
        text = path.read_text()
        rel = path.relative_to(ROOT)
        for include in INCLUDE_RE.findall(text):
            if "firewall/rules/" in include or include.startswith("rules/"):
                problems.append(f"{rel}: includes policy module file '{include}'")
        for module_id in sorted(ids):
            if f'"{module_id}' in text:
                problems.append(f"{rel}: references policy module id '{module_id}'")
        for prefix in ID_PREFIXES:
            if f'"{prefix}' in text:
                problems.append(f"{rel}: references policy module id prefix '{prefix}'")

    if problems:
        print("firewall dependency guard failed:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    print(f"firewall dependency guard: OK ({len(files)} files, "
          f"{len(ids)} module ids)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
