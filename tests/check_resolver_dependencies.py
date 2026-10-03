#!/usr/bin/env python3
"""Dependency guard: the resolver integration stays optional and isolated.

* src/firewall and src/intercept (the daemon-owned dynamic set filling) must
  not include resolver/dnsmasq code: no src/resolver/, no dnsmasq generator,
  no TXT hash client, no Keenetic DNS cache, no system resolver hook and no
  daemon headers.
* The daemon core (src/daemon, minus the resolver helper files that belong to
  the dnsmasq integration) may only reach the resolver through the
  ResolverIntegration interface (resolver/resolver_integration.hpp): it must
  not include resolver/dnsmasq_integration.hpp or the dnsmasq specific dns/
  headers.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

# Headers that belong to the dnsmasq integration.
RESOLVER_SPECIFIC = (
    "resolver/",
    "dns/dnsmasq_gen",
    "dns/dns_txt_client",
    "dns/keenetic_dns",
    "daemon/system_resolver_hook",
    "daemon/resolver_",
    "ipc/resolver_fallback",
    "system_resolver_hook",
    "dnsmasq_gen",
    "dns_txt_client",
    "keenetic_dns",
)

# (glob, forbidden include fragments, allowed exceptions)
RULES = [
    ("firewall/**/*", RESOLVER_SPECIFIC + ("daemon/",), ()),
    ("intercept/**/*", RESOLVER_SPECIFIC + ("daemon/",), ()),
    (
        "daemon/**/*",
        ("resolver/dnsmasq_integration", "dns/dnsmasq_gen", "dns/dns_txt_client",
         "dns/keenetic_dns", "dnsmasq_gen.hpp", "dns_txt_client.hpp", "keenetic_dns.hpp"),
        (),
    ),
]
# Files that implement the dnsmasq side of the integration and live in
# src/daemon for historical reasons.
DAEMON_RESOLVER_FILES = {
    "system_resolver_hook.cpp", "system_resolver_hook.hpp",
    "resolver_apply_confirmation.cpp", "resolver_apply_confirmation.hpp",
    "resolver_health.cpp", "resolver_health.hpp",
    "resolver_stream_wait.cpp", "resolver_stream_wait.hpp",
    "resolver_sync_state_machine.cpp", "resolver_sync_state_machine.hpp",
}


def main():
    problems = []
    checked = 0
    for pattern, forbidden, _ in RULES:
        for path in sorted(SRC.glob(pattern)):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            if pattern.startswith("daemon/") and path.name in DAEMON_RESOLVER_FILES:
                continue
            checked += 1
            rel = path.relative_to(ROOT)
            for include in INCLUDE_RE.findall(path.read_text()):
                normalized = include.replace("../", "")
                for fragment in forbidden:
                    if fragment in normalized:
                        problems.append(f"{rel}: includes '{include}' (matches '{fragment}')")
                        break
    if checked == 0:
        print("resolver dependency guard: no files checked", file=sys.stderr)
        return 2
    if problems:
        print("resolver dependency guard failed:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    print(f"resolver dependency guard: OK ({checked} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
