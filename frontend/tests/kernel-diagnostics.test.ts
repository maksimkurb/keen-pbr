import { describe, expect, test } from "bun:test"

import {
  getKernelBadgeState,
  getVisibleInterceptDiagnosticEntries,
  isHealthyInterceptStatus,
  mapKernelDiagnosticEntries,
} from "../src/lib/intercept-diagnostics"

const caps = { nfqueue: true, nflog: true, connbytes: true }

describe("kernel diagnostics group", () => {
  test("maps capabilities and probes into entries with relevance", () => {
    const entries = mapKernelDiagnosticEntries(
      { nfqueue: true, nflog: false, connbytes: true },
      [
        { feature: "nfqueue", status: "ok" },
        { feature: "nflog", status: "unsupported", reason: "no module" },
        { feature: "fail_open", status: "skipped" },
      ],
      true,
      false
    )
    expect(entries.map((e) => [e.key, e.status, e.relevant])).toEqual([
      ["capability:nfqueue", "ok", true],
      ["capability:nflog", "unsupported", false],
      ["capability:connbytes", "ok", false],
      ["probe:nfqueue", "ok", true],
      ["probe:nflog", "unsupported", false],
      ["probe:fail_open", "skipped", true],
    ])
    expect(entries[4].reason).toBe("no module")
    expect(entries[4].kind).toBe("probe")
  })

  test("classifies healthy statuses and filters them", () => {
    expect(isHealthyInterceptStatus("ok")).toBe(true)
    expect(isHealthyInterceptStatus("skipped")).toBe(true)
    expect(isHealthyInterceptStatus("not_run")).toBe(false)
    expect(isHealthyInterceptStatus("error")).toBe(false)
    expect(isHealthyInterceptStatus("unsupported")).toBe(false)

    const entries = ["ok", "skipped", "not_run", "error", "unsupported"].map(
      (status) => ({ status, relevant: true })
    )
    expect(
      getVisibleInterceptDiagnosticEntries(entries, false).map((e) => e.status)
    ).toEqual(["not_run", "error", "unsupported"])
  })

  const badge = (statuses: string[]) =>
    getKernelBadgeState(statuses.map((status) => ({ status, relevant: true })))

  test("badge state", () => {
    expect(badge(["ok", "ok"])).toBe("healthy")
    expect(badge(["ok", "skipped"])).toBe("healthy")
    expect(badge(["ok", "not_run"])).toBe("neutral")
    expect(badge(["ok", "error"])).toBe("degraded")
    expect(badge(["ok", "unsupported"])).toBe("degraded")
    expect(badge(["not_run", "error"])).toBe("degraded")
  })

  test("badge ignores irrelevant entries", () => {
    const entries = mapKernelDiagnosticEntries(
      { ...caps, nflog: false },
      [{ feature: "nflog", status: "error" }],
      true,
      false
    )
    expect(getKernelBadgeState(entries)).toBe("healthy")
  })
})
