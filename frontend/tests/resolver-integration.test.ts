import { describe, expect, test } from "bun:test"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import { effectiveResolverIntegration } from "../src/api/selectors"
import {
  buildUpdatedConfig,
  getDraftFromConfig,
} from "../src/pages/general-config-page"
import { defaults } from "./settings-defaults"

describe("resolver integration settings", () => {
  test("is the saved value, or none when the field is absent", () => {
    expect(
      effectiveResolverIntegration({
        dns: { rules: [{ list: ["video"], servers: ["wan"] }], fallback: ["wan"] },
      })
    ).toBe("none")
    expect(effectiveResolverIntegration({ dns: { servers: [] } })).toBe("none")
    expect(effectiveResolverIntegration(undefined)).toBe("none")
  })

  test("an explicit mode is returned as is", () => {
    expect(
      effectiveResolverIntegration({
        dns: {
          resolver_integration: "none",
          rules: [{ list: ["video"], servers: ["wan"] }],
        },
      })
    ).toBe("none")
    expect(
      effectiveResolverIntegration({
        dns: { resolver_integration: "dnsmasq" },
      })
    ).toBe("dnsmasq")
  })

  test("saving settings keeps the config version", () => {
    const config: ConfigObject = {
      version: "3.4.0",
      dns: { resolver_integration: "dnsmasq" },
    }
    const updated = buildUpdatedConfig(
      config,
      getDraftFromConfig(config, defaults),
      defaults
    )
    expect(updated.version).toBe("3.4.0")
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
        rules: [{ list: ["video"], servers: ["wan"] }],
        fallback: ["wan"],
        system_resolver: { address: "127.0.0.1" },
        dns_test_server: { listen: "127.0.0.88:12153" },
      },
    }

    const draft = getDraftFromConfig(config, defaults)
    const off = buildUpdatedConfig(
      config,
      {
        ...draft,
        dns: { resolver_integration: "none", dnsmasq_instance: "" },
      },
      defaults
    )
    expect(off.dns?.resolver_integration).toBe("none")
    expect(off.dns?.servers).toEqual(config.dns?.servers)
    expect(off.dns?.rules).toEqual(config.dns?.rules)
    expect(off.dns?.fallback).toEqual(config.dns?.fallback)
    expect(off.dns?.system_resolver).toEqual(config.dns?.system_resolver)
    expect(off.dns?.dns_test_server).toEqual(config.dns?.dns_test_server)

    const on = buildUpdatedConfig(
      off,
      {
        ...getDraftFromConfig(off, defaults),
        dns: { resolver_integration: "dnsmasq", dnsmasq_instance: "" },
      },
      defaults
    )
    expect(on.dns?.resolver_integration).toBe("dnsmasq")
  })
})
