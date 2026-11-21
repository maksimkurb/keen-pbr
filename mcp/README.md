# keen-pbr MCP server

An MCP server that lets Claude (or another MCP client) inspect and change a
keen-pbr router through its REST API. It runs as a separate stdio process and
does not need anything installed on the router.

Full user guide: [English](../docs/content/docs/mcp.md) ·
[Russian](../docs/content/docs/mcp.ru.md).

The server is aimed at two jobs:

- **Diagnostics** — find out why routing or DNS is broken and which
  outbound/interface/list is involved.
- **Rule management** — add domains to lists, create or edit route rules, and
  apply the result.

## Requirements

- Node.js 20 or newer on the machine that runs the MCP client.
- keen-pbr with the REST API enabled (`api.enabled: true` in the router config).
- A keen-pbr API token with at least the `read` scope (`read:prometheus` for
  metrics). Add `write:rules` and `write` if you want the editing tools.

## Install

```sh
cd mcp
npm install
npm run build
```

This produces `mcp/dist/index.js`. Use its absolute path in the client config.

## Environment

| Variable | Required | Meaning |
|---|---|---|
| `KEEN_PBR_URL` | yes | Router API base URL, for example `http://192.168.1.1:12121`. A reverse-proxy path prefix is supported. |
| `KEEN_PBR_TOKEN` | yes | API token. Sent as `Authorization: Bearer <token>`. |
| `KEEN_PBR_ALLOW_WRITE` | no | Set to `1` to enable tools that change config or refresh lists. Without it every mutating tool returns an error. |
| `KEEN_PBR_TIMEOUT_MS` | no | HTTP request timeout, 1000–120000 ms. Default 15000. |

Only a bearer token is supported. The token stays in the MCP server process; it
is never written to the config or included in tool output. Use HTTPS or a
trusted network, because the token travels with every request.

## Claude Desktop

Add this to `claude_desktop_config.json` (`Settings → Developer → Edit Config`):

```json
{
  "mcpServers": {
    "keen-pbr": {
      "command": "node",
      "args": ["/absolute/path/to/keen-pbr/mcp/dist/index.js"],
      "env": {
        "KEEN_PBR_URL": "http://192.168.1.1:12121",
        "KEEN_PBR_TOKEN": "your-api-token",
        "KEEN_PBR_ALLOW_WRITE": "1"
      }
    }
  }
}
```

Restart Claude Desktop after saving. The tools appear under the `keen-pbr`
server.

## Claude Code

```sh
claude mcp add keen-pbr \
  --env KEEN_PBR_URL=http://192.168.1.1:12121 \
  --env KEEN_PBR_TOKEN=your-api-token \
  --env KEEN_PBR_ALLOW_WRITE=1 \
  -- node /absolute/path/to/keen-pbr/mcp/dist/index.js
```

Omit `KEEN_PBR_ALLOW_WRITE` for a read-only setup.

## First steps

Ask Claude:

- “Diagnose keen-pbr” — runs `diagnose` and returns a prioritised verdict.
- “Where does youtube.com go?” — runs `routing_test`.
- “Add example.com to the `my_domains` list and route that list through `vpn`”
  — edits the config draft.

## Config changes are staged

Editing tools do not write to the router immediately. They read the current
config, change an in-memory **draft**, and return a diff. The change takes effect
only after `save_config`:

1. `list_rules` or `get_config` — see lists, outbounds and current rules.
2. `add_domain_to_list`, `create_list`, `create_rule`, `update_rule` or
   `delete_rule` — stage the change.
3. `save_config` — persist and apply it. This waits for the apply and reports
   its final status.
4. `discard_config` — throw the draft away instead.

Each editing tool also accepts `save: true` to save in the same call. Without it
the tool tells you that the change is staged.

Edits are read-modify-write: the server reads the current config, changes it and
stages the whole document. Avoid editing the same config in the web UI at the
same time; the last write wins.

On a router a failed apply can leave a rollback available; `rollback_config`
restores the previous config in that case.

## Tools

### Diagnostics (read-only)

| Tool | What it shows |
|---|---|
| `diagnose` | One-call summary: service, routing, issues, outbounds and interfaces, ranked problems, warning-code titles and a suggested next step. |
| `get_health` | Service health: runtime state, version, draft/rollback flags, interception and dnsmasq state. |
| `get_runtime_outbounds` | Which outbounds are healthy, degraded or unavailable, with their interfaces and latencies. |
| `get_runtime_interfaces` | Interface inventory: up/down, carrier, addresses and Keenetic descriptions. |
| `get_command_failure` | The most recent failed privileged commands, if any are logged. |
| `routing_test` | Where a domain or IP really goes: expected vs live outbound, matched list and rule. |
| `dns_test` | Events observed on the DNS/L7 interception stream for a short time window. |
| `dns_check_servers` | Probes DNS servers for broken or tampered answers. |
| `get_metrics` | A summary of Prometheus metrics with non-zero errors and down probes; use `filter` to inspect one metric family. |

### Config (read-only)

| Tool | What it shows |
|---|---|
| `get_config` | The whole editable config, or one section (`outbounds`, `lists`, `dns`, `route`, …). |
| `list_rules` | Indexed route rules, DNS rules, lists and outbound tags. Rule indexes are used by `update_rule` and `delete_rule`. |

### Lists (write access required)

| Tool | What it does |
|---|---|
| `add_domain_to_list` | Adds domains to a list, creating an inline list when needed. |
| `remove_domain_from_list` | Removes domains from a list's inline entries. |
| `create_list` | Creates a list from domains, IP CIDRs, a local file or a URL. |

### Route rules (write access required)

| Tool | What it does |
|---|---|
| `create_rule` | Adds a route rule, for example “list Y via outbound Z”. |
| `update_rule` | Changes fields of an existing rule by index; pass `null` to remove an optional condition. |
| `delete_rule` | Removes a rule by index. |

### Apply (write access required)

| Tool | What it does |
|---|---|
| `save_config` | Persists and applies the staged draft. |
| `discard_config` | Drops the staged draft. |
| `rollback_config` | Restores the previous config after a failed apply. |
| `refresh_lists` | Re-downloads one URL list, or all of them. |

Service start/stop/restart, authentication endpoints and the status SSE stream
are intentionally not exposed.

## Troubleshooting

- **“KEEN_PBR_URL is required” / “KEEN_PBR_TOKEN is required”** — set the
  variable in the MCP server's `env` block and restart the client.
- **401/403** — the token is wrong or lacks the required scope.
- **“Write access is disabled”** — add `KEEN_PBR_ALLOW_WRITE=1` and restart.
- **409** — a draft or another apply/refresh is already in progress. Run
  `get_config`/`get_health`, then save or discard the draft.
- **Connection error** — check `api.enabled`, the listen address and that the
  router firewall allows the client machine to reach the API port.

## Development

```sh
npm test          # vitest, mocked HTTP layer
npm run build     # tsc -> dist/
npm run api:generate
```

Types come from `docs/openapi.yaml` via `openapi-typescript` into
`src/generated/api-types.ts`. It was chosen over the frontend's Orval setup
because it emits types only, with no runtime and no React Query dependency,
which keeps this small stdio process lean. Regenerate the file after changing
the OpenAPI document; do not edit it by hand.
