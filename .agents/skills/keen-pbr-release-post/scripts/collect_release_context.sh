#!/usr/bin/env bash
set -euo pipefail

base_tag="${1:-}"
head_ref="${2:-HEAD}"

if ! git rev-parse --git-dir >/dev/null 2>&1; then
  echo "error: not inside a git repository" >&2
  exit 1
fi

if [[ -z "${base_tag}" ]]; then
  base_tag="$(git describe --tags --abbrev=0 --match 'v[0-9]*.[0-9]*.[0-9]*' "${head_ref}")"
fi

head_sha="$(git rev-parse --short "${head_ref}")"
origin_url="$(git config --get remote.origin.url || true)"
repo_slug=""

case "${origin_url}" in
  git@github.com:*)
    repo_slug="${origin_url#git@github.com:}"
    repo_slug="${repo_slug%.git}"
    ;;
  https://github.com/*)
    repo_slug="${origin_url#https://github.com/}"
    repo_slug="${repo_slug%.git}"
    ;;
esac

echo "Base tag: ${base_tag}"
echo "Head: ${head_sha} (${head_ref})"
if [[ -n "${repo_slug}" ]]; then
  echo "Compare URL skeleton: https://github.com/${repo_slug}/compare/${base_tag}...<target-tag>"
else
  echo "Compare URL skeleton: <repo-url>/compare/${base_tag}...<target-tag>"
fi

echo
echo "Commits:"
git log --oneline "${base_tag}..${head_ref}"

echo
echo "Changed files:"
git diff --name-status "${base_tag}..${head_ref}"

echo
echo "Changed files by area:"
git diff --name-only "${base_tag}..${head_ref}" | awk '
  function area(path) {
    if (path ~ /^frontend\//) return "Web UI"
    if (path ~ /^docs\// || path ~ /^examples\// || path ~ /(^|\/)(README|CHANGELOG|.*\.md)$/) return "Documentation"
    if (path ~ /^packaging\/openwrt\// || path ~ /^openwrt\// || path ~ /repo|repository/) return "OpenWrt & Repository"
    if (path ~ /^packaging\// || path ~ /^debian\// || path ~ /\.service$/) return "Packaging"
    if (path ~ /^\.github\// || path ~ /^ci\// || path ~ /CMakeLists\.txt$/ || path ~ /^cmake\//) return "CI/Build"
    if (path ~ /keenetic|ndm|ndms/i) return "Keenetic OS"
    if (path ~ /^src\// || path ~ /^include\// || path ~ /^tests\//) return "Core"
    return "Other"
  }
  {
    buckets[area($0)] = buckets[area($0)] "\n  - " $0
  }
  END {
    order[1] = "Core"
    order[2] = "Web UI"
    order[3] = "OpenWrt & Repository"
    order[4] = "Keenetic OS"
    order[5] = "Documentation"
    order[6] = "CI/Build"
    order[7] = "Packaging"
    order[8] = "Other"
    for (i = 1; i <= 8; i++) {
      key = order[i]
      if (key in buckets) {
        print key ":" buckets[key]
      }
    }
  }
'
