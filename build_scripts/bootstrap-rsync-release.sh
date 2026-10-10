#!/usr/bin/env bash

set -euo pipefail

SOURCE_REF_TYPE="${1:?Usage: $0 <source-ref-type> <source-ref-name> [source-pr-number]}"
SOURCE_REF_NAME="${2:?}"
SOURCE_PR_NUMBER="${3:-}"
REPO_PUBLIC_BASE_URL="${REPO_PUBLIC_BASE_URL:-https://repo.keen-pbr.fyi}"
RSYNC_PORT="${RSYNC_PORT:-22}"

: "${RSYNC_HOST:?RSYNC_HOST is required}"
: "${RSYNC_USERNAME:?RSYNC_USERNAME is required}"
: "${RSYNC_TARGET_ROOT:?RSYNC_TARGET_ROOT is required}"
: "${RSYNC_SSH_PRIVATE_KEY:?RSYNC_SSH_PRIVATE_KEY is required}"

sanitize_segment() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]' |
        sed -E 's#[^a-z0-9._-]+#-#g; s#-+#-#g; s#(^-+|-+$)##g'
}

link_name="$(sanitize_segment "$SOURCE_REF_NAME")"
if [[ -z "$link_name" ]]; then
    echo "source_ref_name resolved to an empty repository path" >&2
    exit 1
fi
if [[ "$SOURCE_REF_TYPE" != "branch" ]]; then
    echo "Incremental bootstrap only supports branch publications" >&2
    exit 2
fi

build_number="$(git show -s --format=%ct HEAD)"
source_sha="$(git rev-parse HEAD)"
release_name="${link_name}_${build_number}.release"
target_root="${RSYNC_TARGET_ROOT%/}"
repository_root="$target_root/repository"
release_dir="$repository_root/$release_name"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$release_name"

python3 build_scripts/generate_repository_page.py \
    --root-dir "$stage/$release_name" \
    --repo-dir "$stage" \
    --target-root "$link_name" \
    --public-base-url "$REPO_PUBLIC_BASE_URL" \
    --source-ref-type "$SOURCE_REF_TYPE" \
    --source-ref-name "$SOURCE_REF_NAME" \
    --source-pr-number "$SOURCE_PR_NUMBER" \
    --shared-assets-source build_scripts/repository_assets \
    --logo-source frontend/src/assets/logo.svg \
    --favicon-source docs/static/favicon.ico \
    --keys-manifest-source build_scripts/repository-keys.json \
    --keys-source-dir packages/keys

cp build_scripts/repository_assets/repository-live-index.html "$stage/$release_name/index.html"

SOURCE_REF_TYPE="$SOURCE_REF_TYPE" \
SOURCE_REF_NAME="$SOURCE_REF_NAME" \
SOURCE_PR_NUMBER="$SOURCE_PR_NUMBER" \
SOURCE_SHA="$source_sha" \
BUILD_NUMBER="$build_number" \
LINK_NAME="$link_name" \
REPO_PUBLIC_BASE_URL="$REPO_PUBLIC_BASE_URL" \
python3 - <<'PY' > "$stage/$release_name/.release.json"
import json
import os
from urllib.parse import quote

repo_url = "https://github.com/maksimkurb/keen-pbr"
ref_type = os.environ["SOURCE_REF_TYPE"]
name = os.environ["SOURCE_REF_NAME"]
pr_number = os.environ.get("SOURCE_PR_NUMBER", "")
if ref_type == "tag":
    ref_label = f"Release {name}"
    ref_url = f"{repo_url}/releases/tag/{quote(name, safe='')}"
else:
    ref_label = f"Branch {name}"
    ref_url = f"{repo_url}/tree/{quote(name, safe='/')}"

source = {
    "type": ref_type,
    "name": name,
    "refLabel": ref_label,
    "refUrl": ref_url,
}
if pr_number:
    source["prNumber"] = pr_number
    source["prUrl"] = f"{repo_url}/pull/{quote(pr_number, safe='')}"

payload = {
    "baseUrl": f"{os.environ['REPO_PUBLIC_BASE_URL'].rstrip('/')}/repository/{os.environ['LINK_NAME']}",
    "targetRoot": os.environ["LINK_NAME"],
    "generation": int(os.environ["BUILD_NUMBER"]),
    "sha": os.environ["SOURCE_SHA"],
    "source": source,
}
print(json.dumps(payload, separators=(",", ":")))
PY

printf '# keen-pbr package repository\n\nInstallation instructions: <%s/repository/%s/>\n' \
    "$REPO_PUBLIC_BASE_URL" "$link_name" > "$stage/$release_name/README.md"

[[ "$RSYNC_HOST" =~ ^[A-Za-z0-9][A-Za-z0-9.-]*$ || "$RSYNC_HOST" =~ ^[0-9A-Fa-f:]+$ ]] ||
    { echo "RSYNC_HOST must be a hostname or IP address" >&2; exit 1; }
[[ "$RSYNC_USERNAME" =~ ^[A-Za-z_][A-Za-z0-9_-]*$ ]] ||
    { echo "RSYNC_USERNAME contains unsupported characters" >&2; exit 1; }
[[ "$target_root" =~ ^/?[A-Za-z0-9._/-]+$ && "$target_root" != *".."* ]] ||
    { echo "RSYNC_TARGET_ROOT must be a safe path without '..'" >&2; exit 1; }
[[ "$RSYNC_PORT" =~ ^[0-9]+$ && "$RSYNC_PORT" -ge 1 && "$RSYNC_PORT" -le 65535 ]] ||
    { echo "RSYNC_PORT must be an integer from 1 to 65535" >&2; exit 1; }

install -d -m 700 "$HOME/.ssh"
printf '%s\n' "$RSYNC_SSH_PRIVATE_KEY" > "$HOME/.ssh/id_rsync"
chmod 600 "$HOME/.ssh/id_rsync"
ssh_options=(-i "$HOME/.ssh/id_rsync" -p "$RSYNC_PORT" -o BatchMode=yes -o StrictHostKeyChecking=accept-new)
ssh_target="${RSYNC_USERNAME}@${RSYNC_HOST}"
if [[ "$RSYNC_HOST" == *:* ]]; then
    rsync_target="${RSYNC_USERNAME}@[${RSYNC_HOST}]"
else
    rsync_target="$ssh_target"
fi
rsync_ssh="ssh -i $HOME/.ssh/id_rsync -p $RSYNC_PORT -o BatchMode=yes -o StrictHostKeyChecking=accept-new"

ssh "${ssh_options[@]}" "$ssh_target" \
    "mkdir -p -- '$release_dir' '$target_root/assets' '$target_root/keys' '$repository_root' && touch '$release_dir/ARTIFACTS_WAL.jsonl'"

rsync -a --delete --delay-updates -e "$rsync_ssh" \
    "$stage/assets/" "$rsync_target:$target_root/assets/"
rsync -a --delete --delay-updates -e "$rsync_ssh" \
    "$stage/keys/" "$rsync_target:$target_root/keys/"

printf '%s\n' \
    '<!doctype html>' \
    '<html lang="en">' \
    '<head>' \
    '  <meta charset="utf-8">' \
    '  <meta http-equiv="refresh" content="0; url=https://repo.keen-pbr.fyi/repository/stable/">' \
    '  <link rel="canonical" href="https://repo.keen-pbr.fyi/repository/stable/">' \
    '  <title>keen-pbr repository</title>' \
    '</head>' \
    '<body><p><a href="https://repo.keen-pbr.fyi/repository/stable/">Open the stable keen-pbr repository</a></p></body>' \
    '</html>' > "$stage/index.html"
printf '%s\n' 'RedirectMatch 302 ^/$ https://repo.keen-pbr.fyi/repository/stable/' > "$stage/.htaccess"
rsync -a --delay-updates -e "$rsync_ssh" "$stage/index.html" "$rsync_target:$target_root/index.html"
rsync -a --delay-updates -e "$rsync_ssh" "$stage/.htaccess" "$rsync_target:$target_root/.htaccess"

for file in index.html README.md .release.json; do
    rsync -a --delay-updates -e "$rsync_ssh" \
        "$stage/$release_name/$file" "$rsync_target:$release_dir/$file"
done

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    {
        echo "## Repository release prepared"
        echo
        echo "- Release directory: \`$release_name\`"
        echo "- Live URL after first successful target: [$REPO_PUBLIC_BASE_URL/repository/$link_name/]($REPO_PUBLIC_BASE_URL/repository/$link_name/)"
        echo "- Source: \`$source_sha\`"
    } >> "$GITHUB_STEP_SUMMARY"
fi
