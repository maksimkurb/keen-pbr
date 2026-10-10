import { describe, expect, test } from "bun:test"

import {
  collectInterceptDiagnosticErrors,
  getVisibleInterceptDiagnosticEntries,
} from "../src/lib/intercept-diagnostics"

describe("intercept diagnostics", () => {
  test("reports missing requested capabilities and failed probes", () => {
    expect(
      collectInterceptDiagnosticErrors(
        { nfqueue: false, nflog: true, connbytes: true },
        [
          { feature: "nfqueue", status: "unsupported", reason: "missing" },
          { feature: "set_write", status: "error", reason: "probe failed" },
        ],
        true,
        true
      )
    ).toEqual(["NFQUEUE", "set_write: probe failed"])
  })

  test("does not turn intentionally disabled features into errors", () => {
    expect(
      collectInterceptDiagnosticErrors(
        { nfqueue: false, nflog: false, connbytes: false },
        [
          { feature: "nfqueue", status: "unsupported" },
          { feature: "nflog", status: "error" },
        ],
        false,
        false
      )
    ).toEqual([])
  })

  test("hides healthy relevant checks while retaining failures and unknown states", () => {
    const entries = [
      { name: "nfqueue", relevant: true, status: "ok" },
      { name: "nflog", relevant: true, status: "unsupported" },
      { name: "set_write", relevant: true, status: "not_run" },
      { name: "connbytes", relevant: false, status: "unsupported" },
    ]

    expect(getVisibleInterceptDiagnosticEntries(entries, false)).toEqual([
      entries[1],
      entries[2],
    ])
    expect(getVisibleInterceptDiagnosticEntries(entries, true)).toEqual([
      entries[0],
      entries[1],
      entries[2],
    ])
  })
})
