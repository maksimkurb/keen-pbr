#!/usr/bin/env bash
set -euo pipefail

role=${1:?usage: guest-run.sh <router|client|wan> [backend] [cases]}
backend=${2:-}
integration_cases=${3:-all}
repo=/mnt/payload
seed=/mnt/seed

mkdir -p "$repo" "$seed"
mountpoint -q "$repo" || mount -o ro /dev/vdc "$repo"
mountpoint -q "$seed" || mount /dev/vdb "$seed"

exec > >(tee -a "$seed/provision.log") 2>&1
echo "KPBR_IT_EVENT backend=${backend:-fixture} case=suite stage=provision role=$role status=begin"

case "$role" in
  router) exec bash "$repo/router-run.sh" "$backend" "$integration_cases" ;;
  client) exec bash "$repo/client-run.sh" ;;
  wan) exec bash "$repo/wan-run.sh" ;;
  *) echo "unknown integration VM role: $role" >&2; exit 2 ;;
esac
