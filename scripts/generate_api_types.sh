#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$REPO_ROOT/src/api/generated"
npx --yes quicktype \
  --lang cpp \
  --src "$REPO_ROOT/docs/openapi.yaml" \
  --src-lang schema \
  --namespace keen_pbr3::api \
  --no-boost \
  --code-format with-struct \
  -o "$REPO_ROOT/src/api/generated/api_types.hpp"
