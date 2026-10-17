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

  test("translates a missing Keenetic DNS upstream", () => {
    const [issue] = mapHealthIssues(
      [
        {
          key: "keenetic-dns-vpn_dns",
          severity: "error",
          code: "keenetic_dns_upstream_missing",
          detail_code: "keenetic_dns_upstream_blocked",
          params: {
            tag: "vpn_dns",
            upstreams: "DoT 8.8.8.8@dns.google",
            mode: "block",
          },
          title: "Keenetic DNS upstream not found",
          detail: "`vpn_dns`: DoT 8.8.8.8@dns.google — queries blocked",
        },
      ],
      t
    )
    expect(issue.tone).toBe("bad")
    expect(issue.title).toBe("overview.issues.keeneticDnsUpstreamMissing")
    expect(issue.detail).toBe(
      'overview.issues.keeneticDnsUpstreamBlocked {"tag":"vpn_dns","upstreams":"DoT 8.8.8.8@dns.google"}'
    )
  })
})
