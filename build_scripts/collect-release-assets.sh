#!/usr/bin/env bash

set -euo pipefail

SOURCE_DIR="${1:-release_files}"
TARGET_DIR="${2:-release_assets}"

normalize_version_release() {
    local version_release="$1"
    printf '%s' "${version_release/-r/-}"
}

infer_keenetic_arch_version() {
    local config_name="$1"
    local fallback_version="$2"

    if [[ "$config_name" =~ ^(.+)-([0-9][0-9.]*)$ ]]; then
        printf '%s\n%s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
    else
        printf '%s\n%s\n' "$config_name" "$fallback_version"
    fi
}

release_asset_name() {
    local asset="$1"
    local relative="${asset#"$SOURCE_DIR"/}"
    local os_version=""
    local arch=""
    local os_flavor_hint=""
    local path_parts=()

    IFS=/ read -ra path_parts <<< "$relative"
    if [[ "${path_parts[0]:-}" =~ ^(openwrt|debian|keenetic)$ ]]; then
        os_flavor_hint="${path_parts[0]}"
        os_version="${path_parts[1]:-}"
        arch="${path_parts[2]:-}"
    else
        os_version="${path_parts[0]:-}"
        arch="${path_parts[1]:-}"
    fi

    local filename
    filename="$(basename "$asset")"

    local extension="${filename##*.}"
    local stem="${filename%.*}"
    local package_name=""
    local version_release=""
    local os_flavor=""

    if [[ "$stem" =~ ^(keen-pbr(-headless)?)_([^_]+)_(openwrt|debian|keenetic)_([^_]+)_(.+)$ ]]; then
        package_name="${BASH_REMATCH[1]}"
        version_release="${BASH_REMATCH[3]}"
        os_flavor="${BASH_REMATCH[4]}"
        os_version="${BASH_REMATCH[5]}"
        arch="${BASH_REMATCH[6]}"
    elif [[ "$stem" =~ ^(keen-pbr(-headless)?)_([^_]+)_(openwrt|debian|keenetic)_(.+)$ ]]; then
        package_name="${BASH_REMATCH[1]}"
        version_release="${BASH_REMATCH[3]}"
        os_flavor="${BASH_REMATCH[4]}"
        arch="${BASH_REMATCH[5]}"

        if [ "$os_flavor" = "keenetic" ]; then
            mapfile -t keenetic_parts < <(infer_keenetic_arch_version "$arch" "$os_version")
            arch="${keenetic_parts[0]}"
            os_version="${keenetic_parts[1]}"
        fi
    elif [[ "$stem" =~ ^(keen-pbr(-headless)?)-([0-9].*)$ ]]; then
        package_name="${BASH_REMATCH[1]}"
        version_release="${BASH_REMATCH[3]}"
        os_flavor="openwrt"
    fi

    if [ -z "$package_name" ] || [ -z "$version_release" ]; then
        echo "Unsupported release package filename: $asset" >&2
        exit 1
    fi

    if [ -z "$os_flavor" ]; then
        if [ -n "$os_flavor_hint" ]; then
            os_flavor="$os_flavor_hint"
        else
            case "$extension" in
                apk) os_flavor="openwrt" ;;
                deb) os_flavor="debian" ;;
                ipk)
                    if [[ "$os_version" =~ ^[0-9] ]]; then
                        os_flavor="openwrt"
                    else
                        os_flavor="keenetic"
                    fi
                    ;;
                *)
                    echo "Unsupported release package extension: $asset" >&2
                    exit 1
                    ;;
            esac
        fi
    fi

    if [ -z "$os_version" ] || [ -z "$arch" ]; then
        echo "Could not infer OS version or architecture for: $asset" >&2
        exit 1
    fi

    version_release="$(normalize_version_release "$version_release")"
    printf '%s_%s_%s_%s_%s.%s\n' \
        "$package_name" \
        "$version_release" \
        "$os_flavor" \
        "$os_version" \
        "$arch" \
        "$extension"
}

mkdir -p "$TARGET_DIR"

mapfile -t asset_files < <(
    find "$SOURCE_DIR" -type f \
        \( -name '*.ipk' -o -name '*.apk' -o -name '*.deb' \) \
        | sort
)

if [ "${#asset_files[@]}" -eq 0 ]; then
    echo "No release package files were found under $SOURCE_DIR" >&2
    exit 1
fi

declare -A seen_names=()

for asset in "${asset_files[@]}"; do
    asset_name="$(release_asset_name "$asset")"
    if [ -n "${seen_names[$asset_name]:-}" ]; then
        echo "Duplicate release asset name detected: $asset_name" >&2
        echo "Conflicting files:" >&2
        echo "  ${seen_names[$asset_name]}" >&2
        echo "  $asset" >&2
        exit 1
    fi
    seen_names[$asset_name]="$asset"
    cp "$asset" "$TARGET_DIR/$asset_name"
done
