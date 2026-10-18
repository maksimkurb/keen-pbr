import { describe, expect, test } from "bun:test"

import type { HealthIssue } from "@/api/generated/model"
import { mapHealthIssues } from "@/components/overview/overview-issues"

const t = (key: string, options?: Record<string, unknown>) =>
  options ? `${key} ${JSON.stringify(options)}` : key

describe("mapHealthIssues", () => {
  test("translates kernel extension issues and keeps the daemon's severity", () => {
    const issues: HealthIssue[] = [
      {
        key: "extension:match:multiport",
        severity: "error",
        code: "xt_extension",
        params: { kind: "match", name: "multiport", module: "xt_multiport" },
        title: "iptables match multiport (xt_multiport)",
        detail_code: "xt_extension_required",
        detail: "Kernel module is missing and the current rules need it",
      },
      {
        key: "extension:match:addrtype",
        severity: "warning",
        code: "xt_extension",
        params: { kind: "match", name: "addrtype", module: "xt_addrtype" },
        title: "iptables match addrtype (xt_addrtype)",
        detail_code: "capability_fallback",
      },
    ]
    const mapped = mapHealthIssues(issues, t)
    expect(mapped.map((issue) => [issue.key, issue.tone, issue.detail])).toEqual(
      [
        [
          "extension:match:multiport",
          "bad",
          "overview.issues.xtExtensionRequired",
        ],
        [
          "extension:match:addrtype",
          "warn",
          "overview.issues.capabilityFallback",
        ],
      ]
    )
    expect(mapped[0].title).toContain('"module":"xt_multiport"')
  })

  test("links outbound issues and keeps raw details", () => {
    const [issue] = mapHealthIssues(
      [
        {
          key: "outbound-vpn",
          severity: "warning",
          code: "outbound_degraded",
          params: { tag: "vpn" },
          title: "vpn is degraded",
          detail: "probe timeout",
          outbound: "vpn",
        },
      ],
      t
    )
    expect(issue.href).toBe("/outbounds/vpn/edit")
    expect(issue.detail).toBe("probe timeout")
    expect(issue.title).toBe('overview.issues.outboundDegraded {"tag":"vpn"}')
  })

  test("translates the not-downloaded list warning and keeps the raw error", () => {
    const [issue] = mapHealthIssues(
      [
        {
          key: "list-blocked",
          severity: "warning",
          code: "list_not_downloaded",
          params: { list: "blocked", url: "https://example.com/l.txt" },
          title: "List 'blocked' is not downloaded yet",
          detail: "HTTP 404 (https://example.com/l.txt)",
        },
      ],
      t
    )
    expect(issue.tone).toBe("warn")
    expect(issue.title).toBe(
      'overview.issues.listNotDownloaded {"list":"blocked"}'
    )
    expect(issue.detail).toBe("HTTP 404 (https://example.com/l.txt)")
  })
})
