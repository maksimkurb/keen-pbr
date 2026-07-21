#!/usr/bin/env bash

wait_for_peer_ssh() {
  local host=$1 attempts=${2:-90} delay=${3:-1}
  local attempt
  for attempt in $(seq 1 "$attempts"); do
    if ssh_ready "$host" >/dev/null 2>&1; then return 0; fi
    sleep "$delay"
  done
  return 1
}
