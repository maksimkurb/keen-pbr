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

describe("mapHealthIssues: DNS server not answering", () => {
  const issue: HealthIssue = {
    key: "dns-server-192.0.2.1:53-",
    severity: "warning",
    code: "dns_server_failing",
    params: { tag: "dns_a", address: "192.0.2.1:53", reason: "timeout", count: "10" },
    title: "DNS server 'dns_a' is not answering",
    detail: "no answer in time (192.0.2.1:53)",
  }

  test("links to the DNS servers page and keeps the raw error", () => {
    const [mapped] = mapHealthIssues([issue], t)
    expect(mapped.tone).toBe("warn")
    expect(mapped.title).toBe('overview.issues.dnsServerFailing {"tag":"dns_a"}')
    expect(mapped.detail).toBe("no answer in time (192.0.2.1:53)")
    expect(mapped.href).toBe("/dns-servers")
  })

  test("names the lists left without a working server", () => {
    const [mapped] = mapHealthIssues(
      [
        {
          ...issue,
          severity: "error",
          entries: ["blocked", "default servers"],
        },
      ],
      t
    )
    expect(mapped.tone).toBe("bad")
    expect(mapped.detail).toContain("overview.issues.dnsServerFailingFor")
    expect(mapped.detail).toContain('"lists":"blocked, overview.issues.defaultServers"')
  })
})
