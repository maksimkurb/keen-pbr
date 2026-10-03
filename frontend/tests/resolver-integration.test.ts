import { describe, expect, test } from "bun:test"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import {
  buildUpdatedConfig,
  getDraftFromConfig,
} from "../src/pages/general-config-page"

describe("general settings and removed resolver integration", () => {
  test("keeps dns.servers and does not write removed resolver fields", () => {
    const config: ConfigObject = {
      dns: {
        servers: [{ tag: "wan", type: "static", address: "1.1.1.1" }],
      },
    }

    const updated = buildUpdatedConfig(config, getDraftFromConfig(config))

    expect(updated.dns?.servers).toEqual(config.dns?.servers)
    expect(updated.dns).not.toHaveProperty("resolver_integration")
    expect(updated.dns).not.toHaveProperty("system_resolver")
    expect(updated.dns).not.toHaveProperty("rules")
  })
})
