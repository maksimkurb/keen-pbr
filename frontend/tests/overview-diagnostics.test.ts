import { describe, expect, test } from "bun:test"

import { collectInterceptDiagnosticErrors } from "../src/lib/intercept-diagnostics"

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
})
