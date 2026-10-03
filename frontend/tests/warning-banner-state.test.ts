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
    config_is_draft: false,
    ...overrides,
  }
}

describe("getWarningBannerMode", () => {
  test("is hidden for a healthy converged service", () => {
    expect(getWarningBannerMode(health({}))).toBe("hidden")
    expect(getWarningBannerMode(null)).toBe("hidden")
  })

  test("shows the draft banner for an unapplied config", () => {
    expect(getWarningBannerMode(health({ config_is_draft: true }))).toBe(
      "draft"
    )
  })

  test("shows an error when a rollback is available", () => {
    expect(getWarningBannerMode(health({ rollback_available: true }))).toBe(
      "lifecycle-error"
    )
  })
})
