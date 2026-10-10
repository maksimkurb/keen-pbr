import { describe, expect, test } from "bun:test"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import { effectiveResolverIntegration } from "../src/api/selectors"
import {
  buildUpdatedConfig,
  getDraftFromConfig,
} from "../src/pages/general-config-page"

describe("resolver integration settings", () => {
  test("infers dnsmasq from non-empty DNS rules and defaults to none otherwise", () => {
    expect(
      effectiveResolverIntegration({
        dns: { rules: [{ list: ["video"], server: "wan" }] },
      })
    ).toBe("dnsmasq")
    expect(effectiveResolverIntegration({ dns: { servers: [] } })).toBe("none")
    expect(effectiveResolverIntegration({ dns: { rules: [] } })).toBe("none")
    expect(effectiveResolverIntegration(undefined)).toBe("none")
  })

  test("an explicit mode wins over the rules-based inference", () => {
    expect(
      effectiveResolverIntegration({
        dns: {
          resolver_integration: "none",
          rules: [{ list: ["video"], server: "wan" }],
        },
      })
    ).toBe("none")
    expect(
      effectiveResolverIntegration({
        dns: { resolver_integration: "dnsmasq" },
      })
    ).toBe("dnsmasq")
  })

  test("system_resolver alone no longer enables dnsmasq", () => {
    expect(
      effectiveResolverIntegration({
        dns: { system_resolver: { address: "127.0.0.1" } },
      })
    ).toBe("none")
  })

  test("toggling the mode preserves rules, fallback and deprecated fields", () => {
    const config: ConfigObject = {
      dns: {
        resolver_integration: "dnsmasq",
        servers: [{ tag: "wan", type: "static", address: "1.1.1.1" }],
        rules: [{ list: ["video"], server: "wan" }],
        fallback: ["wan"],
        system_resolver: { address: "127.0.0.1" },
        dns_test_server: { listen: "127.0.0.88:12153" },
      },
    }

    const draft = getDraftFromConfig(config)
    const off = buildUpdatedConfig(config, {
      ...draft,
      dns: { resolver_integration: "none" },
    })
    expect(off.dns?.resolver_integration).toBe("none")
    expect(off.dns?.servers).toEqual(config.dns?.servers)
    expect(off.dns?.rules).toEqual(config.dns?.rules)
    expect(off.dns?.fallback).toEqual(config.dns?.fallback)
    expect(off.dns?.system_resolver).toEqual(config.dns?.system_resolver)
    expect(off.dns?.dns_test_server).toEqual(config.dns?.dns_test_server)

    const on = buildUpdatedConfig(off, {
      ...getDraftFromConfig(off),
      dns: { resolver_integration: "dnsmasq" },
    })
    expect(on.dns?.resolver_integration).toBe("dnsmasq")
  })
})
