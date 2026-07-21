#!/usr/bin/env python3
"""Render the supporters YAML file into the marked README.md section."""

from __future__ import annotations

import argparse
import html
from pathlib import Path
import re
from urllib.parse import quote


START_MARKER = "<!-- SPONSORS-LIST:START -->"
END_MARKER = "<!-- SPONSORS-LIST:END -->"
EXPECTED_KEYS = {"github", "others"}


def _parse_inline_list(value: str, path: Path, number: int) -> list[str]:
    """Parse the small inline-list subset we allow in the sponsors file."""
    if value == "[]":
        return []
    if not (value.startswith("[") and value.endswith("]")):
        raise ValueError(f"{path}:{number}: expected a list value")

    entries = []
    for item in value[1:-1].split(","):
        item = item.strip()
        if len(item) >= 2 and item[0] == item[-1] and item[0] in {"'", '"'}:
            item = item[1:-1]
        if not item:
            raise ValueError(f"{path}:{number}: supporter name cannot be empty")
        entries.append(item)
    return entries


def parse_supporters(path: Path) -> dict[str, list[str]]:
    """Parse the deliberately small YAML schema used by docs/data/sponsors.yaml."""
    supporters = {"github": [], "others": []}
    current_key: str | None = None

    for number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        if not raw_line[0].isspace() and ":" in stripped:
            key, inline_value = (part.strip() for part in stripped.split(":", 1))
            if key not in EXPECTED_KEYS:
                raise ValueError(f"{path}:{number}: unsupported key {key!r}")
            current_key = key
            if inline_value:
                supporters[key].extend(_parse_inline_list(inline_value, path, number))
            continue

        if current_key and stripped.startswith("- "):
            value = stripped[2:].strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
                value = value[1:-1]
            if not value:
                raise ValueError(f"{path}:{number}: supporter name cannot be empty")
            supporters[current_key].append(value)
            continue

        raise ValueError(f"{path}:{number}: expected a key or a list item")

    return supporters


def render_section(supporters: dict[str, list[str]]) -> str:
    lines = [START_MARKER, "# Thanks to all our supporters", ""]

    if supporters["github"]:
        lines.extend(["## GitHub supporters", ""])
        for username in supporters["github"]:
            safe_username = quote(username, safe="-")
            escaped_username = html.escape(username, quote=True)
            lines.append(
                f'[<img src="https://github.com/{safe_username}.png?size=96" '
                f'alt="{escaped_username}" width="80" height="80" />]'
                f'(https://github.com/{safe_username})'
            )
        lines.append("")

    if supporters["others"]:
        lines.extend(["## Other supporters", ""])
        lines.extend(f"- {name}" for name in supporters["others"])
        lines.append("")

    lines.append(END_MARKER)
    return "\n".join(lines)


def replace_section(readme: str, section: str) -> str:
    pattern = re.compile(
        rf"{re.escape(START_MARKER)}.*?{re.escape(END_MARKER)}",
        flags=re.DOTALL,
    )
    updated, replacements = pattern.subn(section, readme, count=1)
    if replacements != 1:
        raise ValueError("README.md must contain one supporters marker pair")
    return updated


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=Path("docs/data/sponsors.yaml"))
    parser.add_argument("--output", type=Path, default=Path("README.md"))
    args = parser.parse_args()

    supporters = parse_supporters(args.input)
    readme = args.output.read_text(encoding="utf-8")
    args.output.write_text(replace_section(readme, render_section(supporters)), encoding="utf-8")


if __name__ == "__main__":
    main()
