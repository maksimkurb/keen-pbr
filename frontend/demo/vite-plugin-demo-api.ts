import type { IncomingMessage, ServerResponse } from "node:http"

import type { Plugin } from "vite"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import type { Outbound } from "../src/api/generated/model/outbound"
import type { RouteRule } from "../src/api/generated/model/routeRule"

import {
  applyDemoConfig,
  getDemoState,
  replaceDemoConfig,
} from "./state"

const JSON_HEADERS = { "Content-Type": "application/json" }

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    req.on("data", (chunk: Buffer) => chunks.push(chunk))
    req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")))
    req.on("error", reject)
  })
}

function sendJson(
  res: ServerResponse,
  status: number,
  payload: unknown,
) {
  res.statusCode = status
  for (const [key, value] of Object.entries(JSON_HEADERS)) {
    res.setHeader(key, value)
  }
  res.end(JSON.stringify(payload))
}

function runtimeInterfaces() {
  return {
    interfaces: [
      {
        name: "ppp0",
        status: "up",
        admin_up: true,
        oper_state: "up",
        carrier: true,
        ipv4_addresses: ["192.0.2.10/24", "192.0.2.11/24"],
        ipv6_addresses: ["2001:db8::1/64"],
      },
      {
        name: "tun0",
        status: "up",
        admin_up: true,
        oper_state: "unknown",
        carrier: true,
        ipv4_addresses: ["10.8.0.2/24"],
      },
      {
        name: "br-lan",
        status: "up",
        admin_up: true,
        oper_state: "up",
        carrier: true,
        ipv4_addresses: ["192.168.1.1/24"],
      },
    ],
  }
}

function runtimeOutbounds() {
  const { config } = getDemoState()
  return {
    outbounds: (config.outbounds ?? []).map((outbound: Outbound) => ({
      tag: outbound.tag,
      type: outbound.type,
      status: outbound.tag === "drop" ? "unavailable" : "healthy",
      interfaces:
        outbound.type === "interface" && outbound.interface
          ? [
              {
                outbound_tag: outbound.tag,
                interface_name: outbound.interface,
                status: "active",
              },
            ]
          : [],
    })),
  }
}

function routingHealth() {
  return {
    overall: "ok",
    firewall_backend: "nftables",
    firewall: {
      chain_present: true,
      prerouting_hook_present: true,
    },
    firewall_rules: [],
    route_tables: [],
    policy_rules: [],
  }
}

function serviceHealth() {
  const state = getDemoState()
  return {
    version: "3.0.1-demo",
    build: "demo-local",
    status: "running",
    os_type: "demo",
    os_version: "local",
    build_variant: "vite-demo",
    resolver_live_status: "healthy",
    resolver_config_sync_state: "converged",
    config_is_draft: state.is_draft,
  }
}

function routingTestResponse(target: string) {
  const { config } = getDemoState()
  const rules = config.route?.rules ?? []
  return {
    target,
    is_domain: !/^\d{1,3}(\.\d{1,3}){3}$/.test(target),
    resolved_ips: ["203.0.113.10"],
    warnings: [],
    no_matching_rule: rules.length === 0,
    rule_diagnostics: rules.map((rule: RouteRule, rule_index: number) => ({
      rule_index,
      outbound: rule.outbound,
      interface_name: "ppp0",
      target_in_lists: Boolean(rule.list?.length),
      ip_rows: [
        {
          ip: "203.0.113.10",
          in_ipset: rule_index === 0,
        },
      ],
    })),
    results: [
      {
        ip: "203.0.113.10",
        expected_outbound: rules[0]?.outbound ?? "wan0",
        actual_outbound: rules[0]?.outbound ?? "wan0",
        ok: true,
      },
    ],
  }
}

async function handleApi(
  req: IncomingMessage,
  res: ServerResponse,
  pathname: string,
) {
  const method = req.method ?? "GET"
  const state = getDemoState()

  if (method === "GET" && pathname === "/api/health/service") {
    sendJson(res, 200, serviceHealth())
    return
  }

  if (method === "GET" && pathname === "/api/health/routing") {
    sendJson(res, 200, routingHealth())
    return
  }

  if (method === "GET" && pathname === "/api/runtime/outbounds") {
    sendJson(res, 200, runtimeOutbounds())
    return
  }

  if (method === "GET" && pathname === "/api/runtime/interfaces") {
    sendJson(res, 200, runtimeInterfaces())
    return
  }

  if (method === "GET" && pathname === "/api/config") {
    sendJson(res, 200, {
      config: state.config,
      is_draft: state.is_draft,
      list_refresh_state: state.list_refresh_state,
    })
    return
  }

  if (method === "POST" && pathname === "/api/config") {
    const raw = await readBody(req)
    const body = JSON.parse(raw || "{}") as ConfigObject
    replaceDemoConfig(body)
    sendJson(res, 200, {
      status: "ok",
      message: "Configuration staged (demo).",
    })
    return
  }

  if (method === "POST" && pathname === "/api/config/save") {
    applyDemoConfig()
    sendJson(res, 200, {
      status: "ok",
      message: "Configuration applied (demo).",
      apply_started_ts: Math.floor(Date.now() / 1000),
    })
    return
  }

  if (method === "POST" && pathname === "/api/lists/refresh") {
    sendJson(res, 200, {
      status: "ok",
      message: "Lists refreshed (demo).",
      refreshed_lists: Object.keys(state.config.lists ?? {}),
      changed_lists: [],
      failed_lists: [],
      reloaded: false,
    })
    return
  }

  if (
    method === "POST" &&
    (pathname === "/api/service/start" ||
      pathname === "/api/service/stop" ||
      pathname === "/api/service/restart")
  ) {
    sendJson(res, 200, {
      status: "ok",
      message: `Service action accepted (demo): ${pathname}`,
    })
    return
  }

  if (method === "POST" && pathname === "/api/routing/test") {
    const raw = await readBody(req)
    const body = JSON.parse(raw || "{}") as { target?: string }
    sendJson(res, 200, routingTestResponse(body.target ?? "example.com"))
    return
  }

  if (method === "GET" && pathname === "/api/dns/test") {
    res.statusCode = 200
    res.setHeader("Content-Type", "text/event-stream")
    res.setHeader("Cache-Control", "no-cache")
    res.setHeader("Connection", "keep-alive")
    res.write('data: {"type":"HELLO"}\n\n')
    res.end()
    return
  }

  sendJson(res, 404, { error: `Demo API: unhandled ${method} ${pathname}` })
}

export function demoApiPlugin(): Plugin {
  return {
    name: "keen-pbr-demo-api",
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const url = req.url ?? ""
        if (!url.startsWith("/api")) {
          next()
          return
        }

        const pathname = url.split("?")[0] ?? url
        void handleApi(req, res, pathname).catch((error: unknown) => {
          const message =
            error instanceof Error ? error.message : "Demo API error"
          sendJson(res, 500, { error: message })
        })
      })
    },
  }
}
