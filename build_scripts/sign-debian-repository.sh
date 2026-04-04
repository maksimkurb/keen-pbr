#!/usr/bin/env bash

set -euo pipefail

RELEASE_DIR="${1:?Usage: $0 <release-dir> [debian-version]}"
DEBIAN_VERSION="${2:-}"
REPO_ROOT="$RELEASE_DIR/debian"
SIGNING_MARKER="$RELEASE_DIR/debian/.signed"

if [ -n "$DEBIAN_VERSION" ]; then
    REPO_ROOT="$REPO_ROOT/$DEBIAN_VERSION"
fi

if [ ! -d "$REPO_ROOT" ]; then
    echo "[sign-debian-repository] Debian repository tree not found at $REPO_ROOT" >&2
    exit 1
fi

if [ -z "${DEBIAN_GPG_PRIVATE_KEY:-}" ]; then
    echo "[sign-debian-repository] DEBIAN_GPG_PRIVATE_KEY is required" >&2
    exit 1
fi

GNUPGHOME="$(mktemp -d)"
export GNUPGHOME
key_file="$(mktemp)"
trap 'rm -rf "$GNUPGHOME" "$key_file"' EXIT

printf '%s\n' "$DEBIAN_GPG_PRIVATE_KEY" > "$key_file"
gpg --batch --import "$key_file"
key_fpr="$(gpg --batch --list-secret-keys --with-colons | awk -F: '$1 == "fpr" { print $10; exit }')"
test -n "$key_fpr" || { echo "[sign-debian-repository] Failed to determine imported GPG key fingerprint" >&2; exit 1; }

rm -f "$SIGNING_MARKER"

find "$REPO_ROOT" -mindepth 2 -maxdepth 3 -type f -name 'Release' | sort | while read -r release_file; do
    release_dir="$(dirname "$release_file")"
    gpg --batch --yes --pinentry-mode loopback --default-key "$key_fpr" \
        --detach-sign -o "$release_dir/Release.gpg" "$release_file"
    gpg --batch --yes --pinentry-mode loopback --default-key "$key_fpr" \
        --clearsign -o "$release_dir/InRelease" "$release_file"
    : > "$SIGNING_MARKER"
done
