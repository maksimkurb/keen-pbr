#!/bin/sh
# install.sh - Script to download and install the latest keenetic-pbr release for OpenWRT

# Function to check for internet connection
check_internet() {
    curl -s "localhost:79/rci/show/internet/status" | grep -E 'gateway-accessible|dns-accessible|internet|captive-accessible' | grep -q false
}

# Function to get the latest release package URL for the correct architecture
get_latest_package_url() {
    ARCH=$(opkg print-architecture | grep -v 'all' | awk 'NR==1{print $2}')
    RELEASE_URL="https://api.github.com/repos/maksimkurb/keen-pbr/releases/latest"
    PACKAGE_URL=$(curl -sH "Accept: application/vnd.github.v3+json" "$RELEASE_URL" | jq -r '.assets[] | select(.name | contains("'"$ARCH"'")) | .browser_download_url')
    echo "$PACKAGE_URL"
}

# Main script starts here
echo "Checking for internet connection..."

if check_internet; then
    echo "[ERROR] No internet connection detected. Please check your connection and try again."
    exit 1
fi

echo "Internet connection available. Proceeding..."

# Get the download URL for the latest release
PACKAGE_URL=$(get_latest_package_url)

if [ -z "$PACKAGE_URL" ]; then
    echo "[ERROR] Could not find a compatible package for your architecture."
    exit 1
fi

PACKAGE_NAME=$(basename "$PACKAGE_URL")

# Download the package
echo "Downloading package: $PACKAGE_NAME"
curl -L -o "/tmp/$PACKAGE_NAME" "$PACKAGE_URL"

# Install the downloaded package
echo "Installing package..."
opkg update
opkg install "/tmp/$PACKAGE_NAME"

if [ $? -eq 0 ]; then
    echo "Package installed successfully."
else
    echo "[ERROR] Failed to install package. Please check the log for details."
fi

# Clean up
rm -f "/tmp/$PACKAGE_NAME"
echo "Installation script completed."
