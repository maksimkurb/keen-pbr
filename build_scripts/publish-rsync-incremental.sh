#!/usr/bin/env bash

set -euo pipefail

SOURCE_REF_NAME="${1:?Usage: $0 <source-ref-name> <platform> <version> <arch>}"
PLATFORM="${2:?}"
VERSION="${3:?}"
ARCH="${4:?}"
REPO_PUBLIC_BASE_URL="${REPO_PUBLIC_BASE_URL:-https://repo.keen-pbr.fyi}"
RSYNC_PORT="${RSYNC_PORT:-22}"

: "${RSYNC_HOST:?RSYNC_HOST is required}"
: "${RSYNC_USERNAME:?RSYNC_USERNAME is required}"
: "${RSYNC_TARGET_ROOT:?RSYNC_TARGET_ROOT is required}"
: "${RSYNC_SSH_PRIVATE_KEY:?RSYNC_SSH_PRIVATE_KEY is required}"

case "$PLATFORM" in
    openwrt|keenetic|debian) ;;
    *) echo "Unsupported platform: $PLATFORM" >&2; exit 2 ;;
esac

sanitize_segment() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]' |
        sed -E 's#[^a-z0-9._-]+#-#g; s#-+#-#g; s#(^-+|-+$)##g'
}

link_name="$(sanitize_segment "$SOURCE_REF_NAME")"
[[ -n "$link_name" ]] || { echo "Empty repository channel" >&2; exit 1; }

build_number="$(git show -s --format=%ct HEAD)"
source_sha="$(git rev-parse HEAD)"
release_name="${link_name}_${build_number}.release"
target_root="${RSYNC_TARGET_ROOT%/}"
repository_root="$target_root/repository"
release_dir="$repository_root/$release_name"
local_leaf="$GITHUB_WORKSPACE/build/packages/$PLATFORM/$VERSION/$ARCH"
remote_leaf="$release_dir/$PLATFORM/$VERSION/$ARCH"

[[ -d "$local_leaf" ]] || {
    echo "Expected package leaf does not exist: $local_leaf" >&2
    find "$GITHUB_WORKSPACE/build/packages" -maxdepth 4 -type d -print 2>/dev/null || true
    exit 1
}

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

ssh "${ssh_options[@]}" "$ssh_target" "test -f '$release_dir/index.html' && mkdir -p -- '$remote_leaf'"
rsync -a --delete --delay-updates -e "$rsync_ssh" "$local_leaf/" "$rsync_target:$remote_leaf/"

if [[ "$PLATFORM" == "keenetic" ]]; then
    debug_leaf="$GITHUB_WORKSPACE/build/packages/keenetic-debug/$VERSION/$ARCH"
    if [[ -d "$debug_leaf" ]]; then
        ssh "${ssh_options[@]}" "$ssh_target" "mkdir -p -- '$release_dir/keenetic-debug/$VERSION/$ARCH'"
        rsync -a --delete --delay-updates -e "$rsync_ssh" "$debug_leaf/" "$rsync_target:$release_dir/keenetic-debug/$VERSION/$ARCH/"
    fi
elif [[ "$PLATFORM" == "openwrt" ]]; then
    debug_dir="$GITHUB_WORKSPACE/build/packages/openwrt-debug/$VERSION"
    if [[ -d "$debug_dir" ]]; then
        mapfile -t debug_files < <(find "$debug_dir" -maxdepth 1 -type f -name "*_${ARCH}_*.debug" -print | sort)
        if (( ${#debug_files[@]} > 0 )); then
            ssh "${ssh_options[@]}" "$ssh_target" "mkdir -p -- '$release_dir/openwrt-debug/$VERSION'"
            rsync -a --delay-updates -e "$rsync_ssh" "${debug_files[@]}" "$rsync_target:$release_dir/openwrt-debug/$VERSION/"
        fi
    fi
fi

formats=()
case "$PLATFORM" in
    openwrt)
        [[ -f "$local_leaf/Packages.gz" ]] && formats+=(opkg)
        [[ -f "$local_leaf/packages.adb" ]] && formats+=(apk)
        ;;
    keenetic) formats+=(opkg) ;;
    debian) formats+=(deb) ;;
esac
(( ${#formats[@]} > 0 )) || { echo "No repository metadata detected in $local_leaf" >&2; exit 1; }
formats_json="$(printf '%s\n' "${formats[@]}" | python3 -c 'import json,sys; print(json.dumps([x.strip() for x in sys.stdin if x.strip()]))')"

record="$(PLATFORM="$PLATFORM" VERSION="$VERSION" ARCH="$ARCH" FORMATS_JSON="$formats_json" BUILD_NUMBER="$build_number" SOURCE_SHA="$source_sha" GITHUB_RUN_ID="${GITHUB_RUN_ID:-0}" GITHUB_RUN_ATTEMPT="${GITHUB_RUN_ATTEMPT:-1}" python3 - <<'PY'
import json, os, time
print(json.dumps({
    "key": f"{os.environ['PLATFORM']}:{os.environ['VERSION']}:{os.environ['ARCH']}",
    "platform": os.environ["PLATFORM"],
    "version": os.environ["VERSION"],
    "arch": os.environ["ARCH"],
    "formats": json.loads(os.environ["FORMATS_JSON"]),
    "generation": int(os.environ["BUILD_NUMBER"]),
    "sha": os.environ["SOURCE_SHA"],
    "run_id": int(os.environ.get("GITHUB_RUN_ID", "0")),
    "run_attempt": int(os.environ.get("GITHUB_RUN_ATTEMPT", "1")),
    "published_at": int(time.time()),
}, separators=(",", ":")))
PY
)"

# Package uploads are independent. Only the append itself is serialized.
printf '%s\n' "$record" | ssh "${ssh_options[@]}" "$ssh_target" \
    "flock -x '$release_dir/.artifacts-wal.lock' tee -a '$release_dir/ARTIFACTS_WAL.jsonl' >/dev/null"

# Prevent a slower older workflow from moving the live branch symlink backwards.
ssh "${ssh_options[@]}" "$ssh_target" bash -s -- "$repository_root" "$link_name" "$release_dir" "$build_number" <<'REMOTE'
set -euo pipefail
repository_root="$1"
link_name="$2"
release_dir="$3"
generation="$4"
exec 9>"$repository_root/.${link_name}.publish.lock"
flock -x 9
current_generation=0
if current_target="$(readlink "$repository_root/$link_name" 2>/dev/null)"; then
    current_base="$(basename "$current_target")"
    if [[ "$current_base" =~ _([0-9]+)\.release$ ]]; then
        current_generation="${BASH_REMATCH[1]}"
    fi
fi
if (( generation >= current_generation )); then
    tmp_link="$repository_root/.${link_name}.new.$$"
    rm -f -- "$tmp_link"
    ln -s -- "$release_dir" "$tmp_link"
    mv -Tf -- "$tmp_link" "$repository_root/$link_name"
    echo "Promoted $link_name -> $release_dir"
else
    echo "Not promoting stale generation $generation; live generation is $current_generation"
fi
REMOTE

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    wal_snapshot="$(mktemp)"
    ssh "${ssh_options[@]}" "$ssh_target" "cat '$release_dir/ARTIFACTS_WAL.jsonl'" > "$wal_snapshot"
    WAL="$wal_snapshot" REPO_URL="$REPO_PUBLIC_BASE_URL/repository/$link_name/" python3 - <<'PY' >> "$GITHUB_STEP_SUMMARY"
import json, os
from pathlib import Path
latest = {}
for line in Path(os.environ["WAL"]).read_text().splitlines():
    if not line.strip(): continue
    try: item = json.loads(line)
    except json.JSONDecodeError: continue
    rank = (int(item.get("generation",0)), int(item.get("run_id",0)), int(item.get("run_attempt",0)), int(item.get("published_at",0)))
    old = latest.get(item.get("key"))
    if old is None or rank >= old[0]: latest[item.get("key")] = (rank, item)
print("## Currently published package targets")
print()
print(f"Repository: [{os.environ['REPO_URL']}]({os.environ['REPO_URL']})")
print()
print("| OS | Version | Architecture | Formats | Commit |")
print("| --- | --- | --- | --- | --- |")
for _, item in sorted(latest.values(), key=lambda p: (p[1].get("platform",""), p[1].get("version",""), p[1].get("arch",""))):
    formats = ", ".join(item.get("formats", []))
    sha = item.get("sha", "")
    print(f"| {item.get('platform','')} | {item.get('version','')} | {item.get('arch','')} | {formats} | `{sha[:12]}` |")
PY
    rm -f "$wal_snapshot"
fi
