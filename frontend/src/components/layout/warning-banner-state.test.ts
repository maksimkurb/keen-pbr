import { describe, expect, test } from "bun:test"

import type { LifecycleOperation } from "@/api/generated/model"
import {
  getWarningBannerMode,
  retainLifecycleOperation,
} from "@/components/layout/warning-banner-state"

function operation(
  id: string,
  status: LifecycleOperation["status"],
  stageStatus: LifecycleOperation["stages"][number]["status"]
): LifecycleOperation {
  return {
    id,
    type: "restart",
    status,
    started_at: 1,
    stages: [{ id: "reload_dnsmasq", title: "Reload", status: stageStatus }],
  }
}

describe("lifecycle operation retention", () => {
  test("keeps an operation across an SSE/query gap", () => {
    const running = operation("one", "running", "running")
    expect(retainLifecycleOperation(running, null)).toEqual(running)
  })

  test("never regresses a completed stage", () => {
    const completed = operation("one", "running", "succeeded")
    const stale = operation("one", "running", "pending")
    expect(retainLifecycleOperation(completed, stale)?.stages[0].status).toBe(
      "succeeded"
    )
  })

  test("a retry operation replaces a retained failure", () => {
    const failed = operation("one", "failed", "failed")
    const retry = operation("two", "running", "pending")
    expect(retainLifecycleOperation(failed, retry)?.id).toBe("two")
  })

  test("retained terminal operations take precedence over standalone warnings", () => {
    expect(getWarningBannerMode(null, operation("one", "failed", "failed"))).toBe(
      "lifecycle-error"
    )
    expect(
      getWarningBannerMode(null, operation("one", "succeeded", "succeeded"))
    ).toBe("lifecycle-success")
  })
})
