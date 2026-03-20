#!/usr/bin/env python3

import json
import subprocess
import sys


def main() -> int:
    version, target, subtarget, repo_root = sys.argv[1:]
    script = f"{repo_root}/.github/builder/generate_build_matrix.py"
    result = subprocess.run(
        ["python3", script, version, target, subtarget],
        check=True,
        capture_output=True,
        text=True,
    )

    job_config = None
    for line in result.stdout.splitlines():
        if line.startswith("job-config="):
            job_config = line[len("job-config="):]
            break

    if job_config is None:
        raise SystemExit("Failed to read job-config from generate_build_matrix.py")

    builds = json.loads(job_config)
    if not builds:
        print("No OpenWrt targets matched the requested filters.")
        return 0

    target_w = max(len("TARGET"), max(len(item["target"]) for item in builds))
    subtarget_w = max(len("SUBTARGET"), max(len(item["subtarget"]) for item in builds))
    pkgarch_w = max(len("PKGARCH"), max(len(item.get("pkgarch") or "-") for item in builds))

    print(f"{'TARGET':<{target_w}}  {'SUBTARGET':<{subtarget_w}}  {'PKGARCH':<{pkgarch_w}}")
    print(f"{'-' * target_w}  {'-' * subtarget_w}  {'-' * pkgarch_w}")
    for item in builds:
        pkgarch = item.get("pkgarch") or "-"
        print(f"{item['target']:<{target_w}}  {item['subtarget']:<{subtarget_w}}  {pkgarch:<{pkgarch_w}}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
