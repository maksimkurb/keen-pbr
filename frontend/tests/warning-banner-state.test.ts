import { describe, expect, test } from "bun:test"

import type { HealthResponse } from "../src/api/generated/model"
import { getWarningBannerMode } from "../src/components/layout/warning-banner-state"

function health(overrides: Partial<HealthResponse>): HealthResponse {
  return {
    version: "test",
    build: "test",
    status: "running",
    os_type: "keenetic",
    os_version: "test",
    build_variant: "test",
    resolver_live_status: "healthy",
    config_is_draft: false,
    ...overrides,
  }
}

describe("getWarningBannerMode", () => {
  test("maps stale resolver sync to restart warning even when live status is degraded", () => {
    expect(
      getWarningBannerMode(
        health({
          resolver_live_status: "degraded",
          resolver_config_probe_status: "missing_txt",
          resolver_config_sync_state: "stale",
        }),
        120_000
      )
    ).toBe("dnsmasq-stale")
  })

  test("maps query failed probe to red error after converge window", () => {
    expect(
      getWarningBannerMode(
        health({
          resolver_live_status: "unavailable",
          resolver_config_probe_status: "query_failed",
          apply_started_ts: 100,
        }),
        120_000
      )
    ).toBe("dnsmasq-error")
  })

  test("keeps query failed probe in converging mode during recent apply", () => {
    expect(
      getWarningBannerMode(
        health({
          resolver_live_status: "unavailable",
          resolver_config_probe_status: "query_failed",
          apply_started_ts: 100,
        }),
        105_000
      )
    ).toBe("dnsmasq-converging")
  })
})
