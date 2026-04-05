#!/usr/bin/env python3

import argparse
import json
import shutil
import sys
from pathlib import Path
from urllib.parse import quote


GITHUB_REPO_URL = "https://github.com/maksimkurb/keen-pbr"
PUBLISHED_KEY_SOURCE_FILES = {
    "debian_public.pem": "debian_public.pem",
    "openwrt_apk_public.pem": "apk_public.pem",
    "openwrt_opkg_public.key": "usign_public.key",
}


def fail(message: str) -> None:
    print(f"[generate-repository-page] ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def replace_tree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        fail(f"required directory is missing: {source}")

    if destination.exists():
        shutil.rmtree(destination)

    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination)


def copy_shared_keys(
    manifest_path: Path, key_source_dir: Path, publish_keys_dir: Path, public_base_url: str
) -> None:
    if not manifest_path.is_file():
        fail(f"missing keys manifest: {manifest_path}")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        fail(f"invalid JSON in {manifest_path}: {exc}")

    if not isinstance(manifest, dict):
        fail(f"{manifest_path} must contain an object at the top level")

    expected_prefix = public_base_url.rstrip("/") + "/keys/"
    for key_name, metadata in manifest.items():
        if not isinstance(metadata, dict):
            fail(f"manifest entry {key_name!r} must be an object")

        key_url = metadata.get("key")
        if not isinstance(key_url, str) or not key_url:
            fail(f"manifest entry {key_name!r} must define a non-empty string key URL")

        if not key_url.startswith(expected_prefix):
            fail(
                f"manifest entry {key_name!r} points outside the published /keys root: {key_url}"
            )

        file_name = key_url[len(expected_prefix) :]
        if not file_name or "/" in file_name:
            fail(f"manifest entry {key_name!r} must point to a direct child under /keys/: {key_url}")

        source_name = PUBLISHED_KEY_SOURCE_FILES.get(file_name)
        if source_name is None:
            fail(
                f"manifest entry {key_name!r} references unsupported published key file: {file_name}"
            )

        if not (key_source_dir / source_name).is_file():
            fail(
                f"manifest entry {key_name!r} references a missing source key file: {source_name}"
            )

    if publish_keys_dir.exists():
        shutil.rmtree(publish_keys_dir)
    publish_keys_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(manifest_path, publish_keys_dir / "keys.json")
    for published_name, source_name in PUBLISHED_KEY_SOURCE_FILES.items():
        shutil.copy2(key_source_dir / source_name, publish_keys_dir / published_name)


def iter_arch_dirs(platform_dir: Path):
    if not platform_dir.is_dir():
        return

    for version_dir in sorted(platform_dir.iterdir()):
        if not version_dir.is_dir():
            continue
        for arch_dir in sorted(version_dir.iterdir()):
            if arch_dir.is_dir():
                yield version_dir.name, arch_dir


def collect_catalog(root_dir: Path, base_url: str) -> dict:
    catalog = {
        "keenetic": [],
        "openwrtOpkg": [],
        "openwrtApk": [],
        "debian": [],
    }

    for version, arch_dir in iter_arch_dirs(root_dir / "openwrt"):
        arch = arch_dir.name
        rel_path = arch_dir.relative_to(root_dir).as_posix()
        if (arch_dir / "Packages.gz").is_file():
            catalog["openwrtOpkg"].append(
                {
                    "version": version,
                    "arch": arch,
                    "feedLine": f"src/gz keen-pbr {base_url}/{rel_path}",
                }
            )
        if (arch_dir / "packages.adb").is_file():
            catalog["openwrtApk"].append(
                {
                    "version": version,
                    "arch": arch,
                    "repositoryUrl": f"{base_url}/{rel_path}/packages.adb",
                }
            )

    for version, arch_dir in iter_arch_dirs(root_dir / "keenetic"):
        arch = arch_dir.name
        rel_path = arch_dir.relative_to(root_dir).as_posix()
        catalog["keenetic"].append(
            {
                "version": version,
                "arch": arch,
                "feedLine": f"src/gz keen-pbr {base_url}/{rel_path}",
            }
        )

    for version, arch_dir in iter_arch_dirs(root_dir / "debian"):
        arch = arch_dir.name
        rel_path = arch_dir.relative_to(root_dir).as_posix()
        catalog["debian"].append(
            {
                "version": version,
                "arch": arch,
                "sourceLine": (
                    "deb [signed-by=/usr/share/keyrings/keen-pbr-archive-keyring.asc] "
                    f"{base_url}/{rel_path} ./"
                ),
            }
        )

    return catalog


def build_source_payload(source_ref_type: str, source_ref_name: str, source_pr_number: str | None) -> dict:
    if source_ref_type == "tag":
        ref_label = f"Release {source_ref_name}"
        ref_url = f"{GITHUB_REPO_URL}/releases/tag/{quote(source_ref_name, safe='')}"
    else:
        ref_label = f"Branch {source_ref_name}"
        ref_url = f"{GITHUB_REPO_URL}/tree/{quote(source_ref_name, safe='/')}"

    payload = {
        "type": source_ref_type,
        "name": source_ref_name,
        "refLabel": ref_label,
        "refUrl": ref_url,
    }

    if source_pr_number:
        payload["prNumber"] = source_pr_number
        payload["prUrl"] = f"{GITHUB_REPO_URL}/pull/{quote(source_pr_number, safe='')}"

    return payload


def write_index_html(root_dir: Path, payload: dict) -> None:
    payload_json = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).replace("</", "<\\/")
    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>keen-pbr repository instructions</title>
  <link rel="stylesheet" href="/assets/repository-page.css">
</head>
<body>
  <div id="app"></div>
  <script id="repository-page-data" type="application/json">{payload_json}</script>
  <script src="/assets/repository-page.js"></script>
  <script>
    window.renderRepositoryInstructions(
      JSON.parse(document.getElementById("repository-page-data").textContent)
    );
  </script>
</body>
</html>
"""
    (root_dir / "index.html").write_text(html, encoding="utf-8")


def write_readme(root_dir: Path, instructions_url: str) -> None:
    readme = "\n".join(
        [
            "# keen-pbr package repository",
            "",
            f"Installation instructions: <{instructions_url}>",
            "",
        ]
    )
    (root_dir / "README.md").write_text(readme, encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate repository instruction pages.")
    parser.add_argument("--root-dir", required=True)
    parser.add_argument("--repo-dir", required=True)
    parser.add_argument("--target-root", required=True)
    parser.add_argument("--public-base-url", required=True)
    parser.add_argument("--source-ref-type", required=True)
    parser.add_argument("--source-ref-name", required=True)
    parser.add_argument("--source-pr-number", default="")
    parser.add_argument("--shared-assets-source", required=True)
    parser.add_argument("--keys-manifest-source", required=True)
    parser.add_argument("--keys-source-dir", required=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    root_dir = Path(args.root_dir).resolve()
    repo_dir = Path(args.repo_dir).resolve()
    shared_assets_source = Path(args.shared_assets_source).resolve()
    keys_manifest_source = Path(args.keys_manifest_source).resolve()
    keys_source_dir = Path(args.keys_source_dir).resolve()
    public_base_url = args.public_base_url.rstrip("/")
    instructions_url = f"{public_base_url}/{args.target_root}/"

    replace_tree(shared_assets_source, repo_dir / "assets")
    copy_shared_keys(keys_manifest_source, keys_source_dir, repo_dir / "keys", public_base_url)

    payload = {
        "baseUrl": f"{public_base_url}/{args.target_root}",
        "targetRoot": args.target_root,
        "source": build_source_payload(
            args.source_ref_type,
            args.source_ref_name,
            args.source_pr_number or None,
        ),
        "catalog": collect_catalog(root_dir, f"{public_base_url}/{args.target_root}"),
    }

    write_index_html(root_dir, payload)
    write_readme(root_dir, instructions_url)


if __name__ == "__main__":
    main()
