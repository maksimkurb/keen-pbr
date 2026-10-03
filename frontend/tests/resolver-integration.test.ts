import { describe, expect, test } from "bun:test"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import { effectiveResolverIntegration } from "../src/api/selectors"
import {
  buildUpdatedConfig,
  getDraftFromConfig,
} from "../src/pages/general-config-page"

describe("resolver integration settings", () => {
  test("infers dnsmasq for legacy DNS rules and defaults to none otherwise", () => {
    expect(
      effectiveResolverIntegration({
        dns: { rules: [{ list: ["video"], server: "wan" }] },
      })
    ).toBe("dnsmasq")
    expect(effectiveResolverIntegration({ dns: { servers: [] } })).toBe("none")
    expect(effectiveResolverIntegration(undefined)).toBe("none")
  })

  test("preserves hidden DNS configuration when switching to none", () => {
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
    draft.resolverIntegration = "none"

    const updated = buildUpdatedConfig(config, draft)

    expect(updated.dns?.resolver_integration).toBe("none")
    expect(updated.dns?.servers).toEqual(config.dns?.servers)
    expect(updated.dns?.rules).toEqual(config.dns?.rules)
    expect(updated.dns?.fallback).toEqual(config.dns?.fallback)
    expect(updated.dns?.system_resolver).toEqual(config.dns?.system_resolver)
    expect(updated.dns?.dns_test_server).toEqual(config.dns?.dns_test_server)
  })
})
