---
title: MCP server
weight: 6
---

The MCP server lets you control keen-pbr from Claude Desktop, Claude Code and other AI clients that support the Model Context Protocol. The assistant can see the routing state, find the cause of a failure, and change lists and rules in plain language, without editing the configuration by hand.

## What you need

- Node.js 20 or newer on the computer that runs the client.
- keen-pbr with the REST API enabled (`api.enabled: true`).
- A keen-pbr API token.

### Create a token

In the keen-pbr web interface, open **Security** > **API tokens**, click **Create token** and select the permissions:

| Permission | What it allows |
|---|---|
| `read` | diagnostics: service, routes, outbounds and interfaces, routing and DNS checks |
| `read:prometheus` | metrics |
| `write:rules` | adding and removing domains, creating and editing lists and rules |
| `write` | saving changes and refreshing lists |

Select all four for full access. If you only need to look around and diagnose, `read` and `read:prometheus` are enough.

Copy the token and keep it somewhere safe. Treat it like a password: do not publish it or send it in chats.

## Install

The server is published on npm as [`keen-pbr-mcp`](https://www.npmjs.com/package/keen-pbr-mcp). Nothing needs to be downloaded or built: the client starts it with `npx` (it comes with Node.js), as shown in the settings below.

## Configure

The server reads its settings from environment variables:

| Variable | Required | Value |
|---|---|---|
| `KEEN_PBR_URL` | yes | Router API address, for example `http://192.168.1.1:12121`. This is the same address you use to open the keen-pbr web interface. |
| `KEEN_PBR_TOKEN` | yes | API token from **Security** > **API tokens**. |
| `KEEN_PBR_ALLOW_WRITE` | no | `1` allows changes. Without it the assistant can only inspect and diagnose. |
| `KEEN_PBR_TIMEOUT_MS` | no | Request timeout in milliseconds, from 1000 to 120000. Default 15000. |

### Claude Desktop

Open `claude_desktop_config.json` (**Settings** → **Developer** → **Edit Config**) and add the server:

```json {filename="claude_desktop_config.json"}
{
  "mcpServers": {
    "keen-pbr": {
      "command": "npx",
      "args": ["-y", "keen-pbr-mcp"],
      "env": {
        "KEEN_PBR_URL": "http://192.168.1.1:12121",
        "KEEN_PBR_TOKEN": "your-api-token",
        "KEEN_PBR_ALLOW_WRITE": "1"
      }
    }
  }
}
```

Restart Claude Desktop. The `keen-pbr` server appears in the tool list.

### Claude Code

```bash {filename="bash"}
claude mcp add keen-pbr \
  --env KEEN_PBR_URL=http://192.168.1.1:12121 \
  --env KEEN_PBR_TOKEN=your-api-token \
  --env KEEN_PBR_ALLOW_WRITE=1 \
  -- npx -y keen-pbr-mcp
```

If you do not need changes, leave out the `KEEN_PBR_ALLOW_WRITE` line.

## Example prompts

- "Why is my traffic not going through the VPN?"
- "Where does youtube.com go?"
- "Add example.com to list my_domains"
- "Create a rule: list my_domains through outbound vpn"
- "Check the DNS servers"
- "Save the changes"

The assistant picks the right tool and shows the list of changes. When everything looks right, say "save" — the changes take effect after saving.

## Tools

### Diagnostics

| Tool | What it shows |
|---|---|
| `diagnose` | The overall picture: what is broken, which outbound, interface or list is affected, and what to check next. |
| `get_health` | Service state, version, runtime mode, DNS interception and dnsmasq status. |
| `get_runtime_outbounds` | Which outbounds are healthy, degraded or unavailable. |
| `get_runtime_interfaces` | Interfaces: whether they are up, whether they have a carrier, and their addresses. |
| `get_command_failure` | The most recent failed service commands, when they are recorded. |
| `routing_test` | Where a domain or IP really goes: expected and actual outbound, matched list and rule. |
| `dns_test` | DNS, TLS and HTTP interception events over a short time window. |
| `dns_check_servers` | Checks whether DNS servers answer and whether their answers look tampered with. |
| `get_metrics` | A short Prometheus metrics summary: errors, failed probes and counters. |

### Configuration

| Tool | What it shows |
|---|---|
| `get_config` | The whole configuration or one section: outbounds, lists, DNS, rules. |
| `list_rules` | Routing rules with their numbers, lists, DNS rules and outbound names. |

### Lists

| Tool | What it does |
|---|---|
| `add_domain_to_list` | Adds domains to a list, creating the list when needed. |
| `remove_domain_from_list` | Removes domains from a list. |
| `create_list` | Creates a list from domains, IP addresses, a file or a URL. |

### Rules

| Tool | What it does |
|---|---|
| `create_rule` | Adds a rule, for example "list my_domains through outbound vpn". |
| `update_rule` | Changes a rule by number: outbound, lists, ports, addresses, DSCP or default gateway mode. An unnecessary condition can be removed. |
| `delete_rule` | Deletes a rule by number. |

### Apply changes

| Tool | What it does |
|---|---|
| `save_config` | Saves and applies the changes. |
| `discard_config` | Cancels unsaved changes. |
| `rollback_config` | Restores the previous configuration after a failed apply. |
| `refresh_lists` | Downloads URL-backed lists again. |

## If something does not work

**The client does not see the server.** Check that the settings run `npx -y keen-pbr-mcp` and that Node.js 20 or newer is installed, then restart the client after changing its settings.

**Cannot connect to the router.** Open the keen-pbr web interface at the same address you put in `KEEN_PBR_URL`. If it does not open, check that the API is enabled and that the port is reachable from the computer.

**401 or 403 error.** The token was copied incompletely or does not have enough permissions. Create a new token with the required permissions.

**The assistant does not change settings.** Add `KEEN_PBR_ALLOW_WRITE=1` to the server environment and restart the client.

**Changes did not take effect.** Ask the assistant to save the configuration — edits take effect after saving.
