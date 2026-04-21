# Firewall Integration Fixtures

This directory contains the Docker + network-namespace integration suite for
real firewall backends.

The goal is:

- use reproducible userspace tools from Docker
- keep one long-lived container per backend
- create a fresh Linux network namespace for each test case
- run the real firewall and routing code path against isolated live state

## What Runs

The suite is built around [`keen-pbr-firewall-it`](../../tests/firewall_integration_main.cpp),
which performs these steps for one fixture:

1. Read the full JSON config file.
2. Parse and validate it with the normal production config code.
3. Allocate outbound fwmarks.
4. Optionally run urltest probes when `--run-urltest-probes` is enabled.
5. Populate real route tables and policy rules through `libnl`.
6. Apply the real firewall backend through the normal runtime path.
7. Verify live firewall chains, live firewall rules, route tables, and policy rules.
8. Exit non-zero on any failure.

The firewall apply path is shared with the daemon through
[`src/firewall/firewall_runtime.cpp`](../../src/firewall/firewall_runtime.cpp).

## Directory Layout

- [`docker/`](./docker): backend-specific Dockerfiles
- [`fixtures/`](./fixtures): one JSON config plus one optional setup script per case
- [`scripts/run-suite.sh`](./scripts/run-suite.sh): builds images, starts one container per backend, runs cases
- [`scripts/run-in-netns.sh`](./scripts/run-in-netns.sh): creates fresh namespaces for one case and runs the harness
- [`scripts/urltest_server.py`](./scripts/urltest_server.py): deterministic in-container HTTP server for urltest cases

## Isolation Model

There are two layers of isolation:

- Docker image: fixes the backend userspace toolchain
- network namespace: resets routes, links, and firewall state for each case

Each case gets a fresh client namespace:

- `run-in-netns.sh` creates `KPBR_CLIENT_NS`
- `lo` is brought up
- the optional fixture setup script provisions interfaces, routes, and servers
- the harness runs with `ip netns exec "$KPBR_CLIENT_NS"`
- namespaces are deleted on exit

Urltest cases may also create a second namespace:

- `KPBR_SERVER_NS`
- typically connected to the client namespace with a `veth` pair
- used to host `urltest_server.py`

Because namespaces are deleted after each case, we do not rely on backend
cleanup alone to reset state.

## How A Fixture Is Composed

Each test case is usually two files:

- `name.json`: full keen-pbr config consumed by the integration binary
- `name.setup.sh`: shell snippet sourced by `run-in-netns.sh` before the harness runs

Example:

- [`fixtures/firewall-smoke.json`](./fixtures/firewall-smoke.json)
- [`fixtures/firewall-smoke.setup.sh`](./fixtures/firewall-smoke.setup.sh)

The JSON fixture should be self-contained and offline:

- use inline lists where possible
- avoid external downloads
- avoid external DNS dependencies
- point `daemon.cache_dir` at a writable in-container path, currently `/tmp/keen-pbr-firewall-it-cache`

The setup script is responsible only for namespace-local topology:

- creating dummy interfaces
- creating `veth` pairs
- assigning IP addresses
- bringing links up
- starting the deterministic local HTTP server for urltest cases

The setup script is sourced, not executed as a subprocess, so it can export
variables or assign `server_pid` for cleanup.

## Current Fixtures

### `firewall-smoke`

Files:

- [`fixtures/firewall-smoke.json`](./fixtures/firewall-smoke.json)
- [`fixtures/firewall-smoke.setup.sh`](./fixtures/firewall-smoke.setup.sh)

Purpose:

- basic routing + firewall apply
- real interface existence and admin-up checks via dummy links
- verifies that a simple interface outbound and list-backed rule can be applied

Topology:

- `lan0` dummy link in the client namespace
- `wan0` dummy link in the client namespace

### `urltest-reachable`

Files:

- [`fixtures/urltest-reachable.json`](./fixtures/urltest-reachable.json)
- [`fixtures/urltest-reachable.setup.sh`](./fixtures/urltest-reachable.setup.sh)

Purpose:

- exercise live urltest probing
- make one child reachable and one child effectively unusable
- verify routing/firewall rebuild using the selected child

Topology:

- client namespace link `wan_fast`
- server namespace link `srv_fast`
- `veth` pair between them on `10.203.0.0/24`
- dummy `wan_dead` interface in the client namespace
- local HTTP server bound to `10.203.0.1:18080`

## How The Runner Works

[`scripts/run-suite.sh`](./scripts/run-suite.sh) does this:

1. Build the `iptables` and `nftables` Docker images.
2. Start one long-lived container for each backend.
3. For each fixture/backend pair, call `run-in-netns.sh` inside that container.
4. Remove the container after that backend finishes.

[`scripts/run-in-netns.sh`](./scripts/run-in-netns.sh) does this for one case:

1. Create a unique client namespace.
2. Bring up `lo`.
3. Export `KPBR_CLIENT_NS` and `KPBR_SERVER_NS`.
4. Source the fixture setup script if provided.
5. Run `keen-pbr-firewall-it` inside the client namespace.
6. Delete namespaces and kill the optional server process on exit.

## Adding A New Test Case

Use this checklist:

1. Add `tests/firewall_it/fixtures/<name>.json`.
2. Add `tests/firewall_it/fixtures/<name>.setup.sh` if the case needs interfaces, routes, or a local server.
3. Keep the fixture offline and deterministic.
4. Add the case to [`scripts/run-suite.sh`](./scripts/run-suite.sh).
5. Run `make firewall-it-build`.
6. Run the suite or the specific `docker exec ... run-in-netns.sh` command for your case.

### JSON Fixture Guidelines

- Include the full config, not a fragment.
- Set `daemon.firewall_backend` to one backend value as a default; `keen-pbr-firewall-it` overrides it from `--backend` at runtime.
- Include `dns.system_resolver.address`, because config validation requires it.
- Use inline `lists` data unless the case specifically needs cached remote-list behavior.
- Use explicit outbound tags and interface names that match what your setup script creates.
- For urltest cases, point `url` to the in-container deterministic HTTP server, not the public internet.

### Setup Script Guidelines

- Do not use `#!/bin/sh` assumptions that depend on being executed directly; the script is sourced by Bash.
- Use `ip netns exec "$KPBR_CLIENT_NS" ...` for client-side topology.
- If you need a server namespace, create `KPBR_SERVER_NS` yourself.
- If you start a background server, assign its PID to `server_pid` so `run-in-netns.sh` can clean it up.
- Keep all addresses and ports stable so failures are easy to reproduce.

### Minimal Smoke Case Template

```json
{
  "daemon": {
    "cache_dir": "/tmp/keen-pbr-firewall-it-cache",
    "firewall_backend": "iptables"
  },
  "outbounds": [
    {
      "tag": "wan0",
      "type": "interface",
      "interface": "wan0"
    }
  ],
  "route": {
    "rules": [
      {
        "outbound": "wan0",
        "dest_addr": "1.1.1.1"
      }
    ]
  },
  "dns": {
    "system_resolver": {
      "address": "127.0.0.1"
    }
  }
}
```

```bash
ip netns exec "$KPBR_CLIENT_NS" ip link add wan0 type dummy
ip netns exec "$KPBR_CLIENT_NS" ip link set wan0 up
```

## Running The Suite

Build the harness:

```sh
make firewall-it-build
```

Build images:

```sh
make firewall-it-images
```

Run the full suite:

```sh
make firewall-it
```

Run one case manually inside a backend container:

```sh
docker exec keen-pbr-firewall-it-iptables \
  /opt/keen-pbr/firewall-it/scripts/run-in-netns.sh \
  --backend iptables \
  --config /opt/keen-pbr/firewall-it/fixtures/firewall-smoke.json \
  --setup /opt/keen-pbr/firewall-it/fixtures/firewall-smoke.setup.sh
```

For urltest cases, add:

```sh
--run-urltest-probes
```

## Notes And Current Limitations

- The harness uses the real firewall backend and real kernel routing state.
- Urltest support in the integration harness is currently synchronous and minimal; it does not run the daemon scheduler loop.
- `run-suite.sh` currently enumerates fixtures explicitly. If the list grows, it may be worth moving to a manifest or filename discovery pattern.
- The harness assumes Docker containers run with enough privileges for `ip netns`, link creation, firewall operations, and `SO_MARK`.
