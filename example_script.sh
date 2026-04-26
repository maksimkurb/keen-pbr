#!/bin/bash

RELEASE_URL="https://archive.openwrt.org/releases/25.12.1"
TARGETS_URL="${RELEASE_URL}/targets/"

echo "Scanning targets for unique architectures..."

# Get all target paths
TARGET_PATHS=$(curl -sL "$TARGETS_URL" | grep -oE 'href="[a-zA-Z0-9_-]+/"' | sed 's/href="//;s/"//' | grep -v "\.\.")

declare -A seen_archs

for target in $TARGET_PATHS; do
    SUBTARGET_PATHS=$(curl -sL "${TARGETS_URL}${target}" | grep -oE 'href="[a-zA-Z0-9_-]+/"' | sed 's/href="//;s/"//' | grep -v "\.\.")
    
    for subtarget in $SUBTARGET_PATHS; do
        # Fetch the profiles.json for this subtarget
        PROFILES_URL="${TARGETS_URL}${target}${subtarget}profiles.json"
        
        # Extract the package architecture
        ARCH=$(curl -sL "$PROFILES_URL" | grep '"arch_packages"' | head -n 1 | awk -F'"' '{print $4}')
        
        if [ -n "$ARCH" ] &&[ -z "${seen_archs[$ARCH]}" ]; then
            seen_archs[$ARCH]=1
            # Just print the architecture name
            echo "$ARCH"
        fi
    done
done
