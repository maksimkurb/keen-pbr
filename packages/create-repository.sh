#!/bin/bash

set -e -x

# Script usage: create-repository.sh <openwrt_version_git_ref> <router>
if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <openwrt_version_git_ref> <router>"
  exit 1
fi

openwrt_version_git_ref="$1"
router="$2"

if [[ -z "$openwrt_version_git_ref" ]]; then
  echo "Error: openwrt_version_git_ref cannot be empty"
  echo "Usage: $0 <openwrt_version_git_ref> <router>"
  exit 1
fi

if [[ -z "$router" ]]; then
  echo "Error: router cannot be empty"
  echo "Usage: $0 <openwrt_version_git_ref> <router>"
  exit 1
fi

BASE_DIR="./release_files/${openwrt_version_git_ref}-${router}"

mkdir -p "$BASE_DIR"
find ./bin -name "*.ipk" -type f | while read file; do
  filename=$(basename "$file")
  newname="${filename%.ipk}-${openwrt_version_git_ref}-${router}.ipk"
  cp "$file" "$BASE_DIR/$newname"
done

# -- apt-get install coreutils

# cd $BASE_DIR
./scripts/ipkg-make-index.sh $BASE_DIR > Packages
# usign -S -m Packages -s ../keys/private.key -x Packages.sig
# gzip -fk Packages
