import { describe, expect, test } from "bun:test"

import type { InterceptHealth, XtExtensionHealth } from "@/api/generated/model"
import { collectOverviewIssues } from "@/components/overview/overview-issues"
import {
  getKernelBadgeState,
  mapKernelDiagnosticEntries,
} from "@/lib/intercept-diagnostics"

const extension = (
  name: string,
  available: boolean,
  required: boolean
): XtExtensionHealth => ({
  name,
  kind: "match",
  kernel_module: `xt_${name}`,
  available,
  required,
})

const capabilities = (xt_extensions?: XtExtensionHealth[]) => ({
  nfqueue: true,
  nflog: true,
  connbytes: true,
  addrtype: false,
  xt_extensions,
})

describe("iptables extension entries", () => {
  test("a missing extension the rules use is an error, an unused one warns", () => {
    const entries = mapKernelDiagnosticEntries(
      capabilities([
        extension("multiport", false, true),
        extension("dscp", false, false),
        extension("set", true, true),
      ]),
      [],
      false,
      false
    )
    const byName = Object.fromEntries(
      entries
        .filter((entry) => entry.kind === "extension")
        .map((entry) => [entry.feature, entry])
    )
    expect(byName.multiport.status).toBe("unsupported")
    expect(byName.multiport.advisory).toBe(false)
    expect(byName.dscp.advisory).toBe(true)
    expect(byName.set.status).toBe("ok")
    expect(getKernelBadgeState(entries)).toBe("degraded")
  })

  test("the extension list replaces the separate addrtype capability", () => {
    const keys = (xt?: XtExtensionHealth[]) =>
      mapKernelDiagnosticEntries(capabilities(xt), [], false, false).map(
        (entry) => entry.key
      )
    expect(keys()).toContain("capability:addrtype")
    expect(keys([extension("addrtype", false, false)])).toEqual([
      "capability:nfqueue",
      "capability:nflog",
      "capability:connbytes",
      "extension:match:addrtype",
    ])
  })

  test("overview issues name the module and the consequence", () => {
    const issues = collectOverviewIssues({
      serviceRunning: false,
      intercept: {
        capabilities: capabilities([
          extension("multiport", false, true),
          extension("dscp", false, false),
          extension("addrtype", false, false),
        ]),
        probes: [],
      } as unknown as InterceptHealth,
      requestedDns: false,
      requestedL7: false,
      runtimeOutbounds: [],
      t: (key, options) =>
        options ? `${key} ${JSON.stringify(options)}` : key,
    })
    expect(
      issues.map((issue) => [issue.key, issue.tone, issue.detail])
    ).toEqual([
      [
        "extension:match:multiport",
        "bad",
        "overview.issues.xtExtensionRequired",
      ],
      ["extension:match:dscp", "warn", "overview.issues.xtExtensionUnused"],
      [
        "extension:match:addrtype",
        "warn",
        "overview.issues.capabilityFallback",
      ],
    ])
    expect(issues[0].title).toContain('"module":"xt_multiport"')
  })
})
