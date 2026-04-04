#!/bin/bash
# build-repository.sh — Assemble a versioned package repository tree.
#
# Usage: scripts/build-repository.sh \
#          <release-dir> <repo-dir> <target-root>
#
# Replaces <repo-dir>/<target-root>/ with the structured package tree from
# <release-dir>, following the layout described in docs/repository-layout.md.
#
# <release-dir>       Directory containing the structured package tree
# <repo-dir>          Root of the repository tree (git worktree or plain directory)
# <target-root>       Destination root inside the repository branch,
#                     e.g. "stable" or "feature_unify_packaging"

set -euo pipefail

RELEASE_DIR="${1:?Usage: $0 <release-dir> <repo-dir> <target-root>}"
REPO_DIR="${2:?}"
TARGET_ROOT_INPUT="${3:?}"
REPO_PUBLIC_BASE_URL="${REPO_PUBLIC_BASE_URL:-https://repo.keen-pbr.fyi}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

sanitize_segment() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | sed -E 's#[^a-z0-9._-]+#-#g; s#-+#-#g; s#(^-+|-+$)##g'
}

sanitize_path() {
    local raw="$1"
    local out=""
    local IFS='/'
    read -ra parts <<< "$raw"
    for part in "${parts[@]}"; do
        [ -z "$part" ] && continue
        part=$(sanitize_segment "$part")
        [ -z "$part" ] && continue
        if [ -z "$out" ]; then
            out="$part"
        else
            out="$out/$part"
        fi
    done
    printf '%s' "$out"
}

print_markdown_table_header() {
    local third_column="$1"
    printf '| Version | Architecture | %s |\n' "$third_column"
    echo '| --- | --- | --- |'
}

TARGET_ROOT=$(sanitize_path "$TARGET_ROOT_INPUT")
if [ -z "$TARGET_ROOT" ]; then
    echo "[build-repository] ERROR: target root resolved to an empty path" >&2
    exit 1
fi

ROOT_DIR="$REPO_DIR/$TARGET_ROOT"
rm -rf "$ROOT_DIR"
mkdir -p "$ROOT_DIR"

for platform in openwrt keenetic debian; do
    if [ -d "$RELEASE_DIR/$platform" ]; then
        mkdir -p "$ROOT_DIR/$platform"
        cp -a "$RELEASE_DIR/$platform"/. "$ROOT_DIR/$platform"/
    fi
done

OPENWRT_USIGN_PUBLIC_KEY="$SCRIPT_DIR/../packages/keys/public/openwrt-usign.pub"
OPENWRT_APK_PUBLIC_KEY="$SCRIPT_DIR/../packages/keys/public/openwrt-apk.pem"
DEBIAN_PUBLIC_KEY="$SCRIPT_DIR/../packages/keys/public/debian-repo.asc"

mkdir -p "$ROOT_DIR/keys"
copied_keys=0
for key_file in "$OPENWRT_USIGN_PUBLIC_KEY" "$OPENWRT_APK_PUBLIC_KEY" "$DEBIAN_PUBLIC_KEY"; do
    if [ -f "$key_file" ]; then
        cp "$key_file" "$ROOT_DIR/keys/"
        copied_keys=1
    fi
done
if [ "$copied_keys" -eq 0 ]; then
    rmdir "$ROOT_DIR/keys" 2>/dev/null || true
fi

OPENWRT_HAS_IPK=0
OPENWRT_HAS_IPK_SIG=0
OPENWRT_HAS_APK=0
OPENWRT_HAS_APK_SIG=0
DEBIAN_HAS_SIG=0

if find "$ROOT_DIR/openwrt" -mindepth 2 -maxdepth 2 -type d -exec test -f '{}/Packages.gz' ';' -print -quit 2>/dev/null | grep -q .; then
    OPENWRT_HAS_IPK=1
fi
if find "$ROOT_DIR/openwrt" -mindepth 2 -maxdepth 2 -type d -exec test -f '{}/Packages.sig' ';' -print -quit 2>/dev/null | grep -q .; then
    OPENWRT_HAS_IPK_SIG=1
fi
if find "$ROOT_DIR/openwrt" -mindepth 2 -maxdepth 2 -type d -exec test -f '{}/packages.adb' ';' -print -quit 2>/dev/null | grep -q .; then
    OPENWRT_HAS_APK=1
fi
if [ -f "$ROOT_DIR/openwrt/.apk-signed" ]; then
    OPENWRT_HAS_APK_SIG=1
fi
if find "$ROOT_DIR/debian" -mindepth 2 -maxdepth 2 -type d \( -exec test -f '{}/InRelease' ';' -o -exec test -f '{}/Release.gpg' ';' \) -print -quit 2>/dev/null | grep -q .; then
    DEBIAN_HAS_SIG=1
fi

find "$ROOT_DIR" -type f \( -name '.apk-signed' -o -name '.signed' \) -delete

RAW_BASE_URL="$REPO_PUBLIC_BASE_URL"
RAW_BASE_URL="$RAW_BASE_URL/$TARGET_ROOT"
INSTRUCTIONS_FILE="$ROOT_DIR/README.md"

{
    echo "# keen-pbr package repository"
    echo
    echo "Repository root for \`$TARGET_ROOT\`."
    echo
    echo "Base URL:"
    echo
    echo "\`$RAW_BASE_URL\`"
    echo

    echo "## OpenWrt"
    if find "$ROOT_DIR/openwrt" -mindepth 2 -maxdepth 2 -type d >/dev/null 2>&1; then
        echo
        if [ "$OPENWRT_HAS_IPK" -eq 1 ]; then
            echo "### OpenWrt opkg/ipk"
            echo
            if [ "$OPENWRT_HAS_IPK_SIG" -eq 1 ] && [ -f "$ROOT_DIR/keys/openwrt-usign.pub" ]; then
                echo "Install the repository signing key first:"
                echo
                echo '```sh'
                printf 'wget %s/keys/openwrt-usign.pub -O /tmp/openwrt-usign.pub\n' "$RAW_BASE_URL"
                echo 'opkg-key add /tmp/openwrt-usign.pub'
                echo '```'
                echo
            fi
            printf 'Open `%s` and add one of these lines:\n' '/etc/opkg/customfeeds.conf'
            echo
            print_markdown_table_header "Feed line"
            find "$ROOT_DIR/openwrt" -mindepth 2 -maxdepth 2 -type d | sort | while read -r arch_dir; do
                [ -f "$arch_dir/Packages.gz" ] || continue
                rel_path="${arch_dir#$ROOT_DIR/}"
                version_dir="$(basename "$(dirname "$arch_dir")")"
                arch_name="$(basename "$arch_dir")"
                printf '| `%s` | `%s` | `%s` |\n' "$version_dir" "$arch_name" "src/gz keen-pbr $RAW_BASE_URL/$rel_path"
            done
            echo
            echo "Then run:"
            echo
            echo '```sh'
            echo 'opkg update'
            echo 'opkg install keen-pbr'
            echo '```'
            echo
        fi
        if [ "$OPENWRT_HAS_APK" -eq 1 ]; then
            echo "### OpenWrt apk"
            echo
            if [ "$OPENWRT_HAS_APK_SIG" -eq 1 ] && [ -f "$ROOT_DIR/keys/openwrt-apk.pem" ]; then
                echo "Install the repository signing key first:"
                echo
                echo '```sh'
                printf 'wget %s/keys/openwrt-apk.pem -O /etc/apk/keys/openwrt-apk.pem\n' "$RAW_BASE_URL"
                echo "echo '/etc/apk/keys/openwrt-apk.pem' >> /etc/sysupgrade.conf"
                echo '```'
                echo
            fi
            echo "Add one of these repository URLs:"
            echo
            print_markdown_table_header "Repository URL"
            find "$ROOT_DIR/openwrt" -mindepth 2 -maxdepth 2 -type d | sort | while read -r arch_dir; do
                [ -f "$arch_dir/packages.adb" ] || continue
                rel_path="${arch_dir#$ROOT_DIR/}"
                version_dir="$(basename "$(dirname "$arch_dir")")"
                arch_name="$(basename "$arch_dir")"
                printf '| `%s` | `%s` | `%s` |\n' "$version_dir" "$arch_name" "$RAW_BASE_URL/$rel_path/packages.adb"
            done
            echo
            echo "Then run:"
            echo
            echo '```sh'
            echo "# Add the selected packages.adb URL to /etc/apk/repositories.d/customfeeds.list"
            echo 'apk update'
            echo 'apk add keen-pbr'
            echo '```'
        fi
    else
        echo
        echo "No OpenWrt packages published in this target."
    fi

    echo
    echo "## Keenetic Entware"
    if find "$ROOT_DIR/keenetic" -mindepth 2 -maxdepth 2 -type d >/dev/null 2>&1; then
        echo
        printf 'Open `%s` and add one of these lines:\n' '/opt/etc/opkg/customfeeds.conf'
        echo
        print_markdown_table_header "Feed line"
        find "$ROOT_DIR/keenetic" -mindepth 2 -maxdepth 2 -type d | sort | while read -r arch_dir; do
            rel_path="${arch_dir#$ROOT_DIR/}"
            version_dir="$(basename "$(dirname "$arch_dir")")"
            arch_name="$(basename "$arch_dir")"
            printf '| `%s` | `%s` | `%s` |\n' "$version_dir" "$arch_name" "src/gz keen-pbr $RAW_BASE_URL/$rel_path"
        done
        echo
        echo "Then run:"
        echo
        echo '```sh'
        echo 'opkg update'
        echo 'opkg install keen-pbr'
        echo '```'
    else
        echo
        echo "No Keenetic Entware packages published in this target."
    fi

    echo
    echo "## Debian"
    if find "$ROOT_DIR/debian" -mindepth 2 -maxdepth 2 -type d >/dev/null 2>&1; then
        echo
        if [ "$DEBIAN_HAS_SIG" -eq 1 ] && [ -f "$ROOT_DIR/keys/debian-repo.asc" ]; then
            echo "Install the repository signing key first:"
            echo
            echo '```sh'
            printf 'wget %s/keys/debian-repo.asc -O /usr/share/keyrings/keen-pbr-archive-keyring.asc\n' "$RAW_BASE_URL"
            echo 'chmod 0644 /usr/share/keyrings/keen-pbr-archive-keyring.asc'
            echo '```'
            echo
        fi
        printf 'Open `%s` and add one of these lines:\n' '/etc/apt/sources.list.d/keen-pbr.list'
        echo
        print_markdown_table_header "Source line"
        find "$ROOT_DIR/debian" -mindepth 2 -maxdepth 2 -type d | sort | while read -r arch_dir; do
            rel_path="${arch_dir#$ROOT_DIR/}"
            version_dir="$(basename "$(dirname "$arch_dir")")"
            arch_name="$(basename "$arch_dir")"
            if [ "$DEBIAN_HAS_SIG" -eq 1 ] && [ -f "$ROOT_DIR/keys/debian-repo.asc" ]; then
                printf '| `%s` | `%s` | `%s` |\n' "$version_dir" "$arch_name" "deb [signed-by=/usr/share/keyrings/keen-pbr-archive-keyring.asc] $RAW_BASE_URL/$rel_path ./"
            else
                printf '| `%s` | `%s` | `%s` |\n' "$version_dir" "$arch_name" "deb [trusted=yes] $RAW_BASE_URL/$rel_path ./"
            fi
        done
        echo
        echo "Then run:"
        echo
        echo '```sh'
        echo 'apt update'
        echo 'apt install keen-pbr'
        echo '```'
    else
        echo
        echo "No Debian packages published in this target."
    fi
} > "$INSTRUCTIONS_FILE"

echo "[build-repository] Repository tree written to $ROOT_DIR"
