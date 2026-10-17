import { expect, test } from "bun:test"
import { createInstance } from "i18next"
import { I18nextProvider } from "react-i18next"
import { renderToStaticMarkup } from "react-dom/server"
import { Router } from "wouter"
import type {
  RoutingTestResponse,
  RoutingTestRuleDiagnostic,
} from "../src/api/generated/model"
import { enTranslation } from "../src/i18n/en"
import { RoutingRouteSummary } from "../src/components/overview/routing-route-summary"
import {
  getExpectedRoutingRule,
  getRuleConditions,
} from "../src/components/overview/routing-diagnostics-utils"

const stripTags = (html: string) => html.replace(/<[^>]+>/g, "")

const i18n = createInstance()
await i18n.init({
  lng: "en",
  resources: { en: { translation: enTranslation } },
  fallbackLng: false,
})
const result = {
  ip: "1.2.3.4",
  expected_outbound: "vpn",
  actual_outbound: "wan",
  ok: false,
  list_match: { list: "google", via: "google.com" },
}
const rule: RoutingTestRuleDiagnostic = {
  rule_index: 2,
  outbound: "vpn",
  interface_name: "",
  target_in_lists: true,
  rule: { outbound: "vpn", list: ["google"] },
  ip_rows: [{ ip: result.ip, in_lists: true, in_ipset: false }],
}
const diagnostics: RoutingTestResponse = {
  target: "google.com",
  is_domain: true,
  resolved_ips: [result.ip],
  warnings: [],
  no_matching_rule: false,
  ipv6_enabled: true,
  results: [result],
  rule_diagnostics: [rule],
}
function render(value: RoutingTestResponse) {
  return renderToStaticMarkup(
    <I18nextProvider i18n={i18n}>
      <Router ssrPath="/">
        <RoutingRouteSummary
          diagnostics={value}
          runtimeOutbounds={[
            {
              tag: "vpn",
              type: "urltest",
              status: "healthy",
              interfaces: [
                {
                  outbound_tag: "warp",
                  interface_name: "vpn_cf",
                  status: "active",
                },
                {
                  outbound_tag: "backup",
                  interface_name: "vpn_backup",
                  status: "backup",
                },
              ],
            },
            {
              tag: "wan",
              type: "interface",
              status: "healthy",
              interfaces: [
                {
                  outbound_tag: "wan",
                  interface_name: "eth1",
                  status: "active",
                },
              ],
            },
          ]}
        />
      </Router>
    </I18nextProvider>
  )
}

test("expected rule respects order, disabled rules and per-IP matches", () => {
  const earlier = { ...rule, rule_index: 1 }
  const disabled = {
    ...rule,
    rule_index: 0,
    rule: { ...rule.rule, enabled: false },
  }
  expect(getExpectedRoutingRule([rule, disabled, earlier], result)).toBe(
    earlier
  )
  expect(
    getExpectedRoutingRule([rule], { ...result, ip: "5.6.7.8" })
  ).toBeUndefined()
  expect(
    getExpectedRoutingRule([rule], { ...result, expected_outbound: "other" })
  ).toBeUndefined()
})

test("server-selected rule takes priority when earlier rule fails packet criteria", () => {
  const earlier = {
    ...rule,
    rule_index: 1,
    ip_rows: [{ ...rule.ip_rows[0], criteria_match: false }],
  }
  const selected = { ...result, matched_rule_index: 2 }
  expect(getExpectedRoutingRule([earlier, rule], selected)).toBe(rule)
  expect(getExpectedRoutingRule([earlier, rule], result)).toBe(rule)
})

test("address and port lists in rule conditions get a space to wrap at", () => {
  expect(
    getRuleConditions({
      outbound: "vpn",
      dest_addr: "188.235.151.99,185.199.108.153",
      dest_port: "80, 443",
    })
  ).toEqual([
    { key: "destinationIp", value: "188.235.151.99, 185.199.108.153" },
    { key: "destinationPort", value: "80, 443" },
  ])
})

test("DSCP appears in the explanation of rule conditions", () => {
  expect(getRuleConditions({ outbound: "vpn", dscp: 46 })).toContainEqual({
    key: "dscp",
    value: "46",
  })
})

test("summary explains expected route, active candidates and missing-IPSet mismatch", () => {
  const html = render(diagnostics)
  expect(html).toContain("#3")
  expect(html).toContain("google.com")
  expect(html).toContain("google (entry <code")
  expect(html).toMatch(/<code[^>]*>google\.com<\/code>/)
  expect(html).toContain("vpn currently uses warp (interface vpn_cf).")
  expect(html).not.toContain("vpn_backup")
  expect(html).toContain("wan traffic leaves through interface eth1.")
  expect(html).not.toContain(
    "Check applied configuration and list updates, then try again."
  )
  expect(html).not.toContain("Step 1 of 4")
  expect(html).toContain("DNS resolution")
  expect(html).toContain("IP not in the expected ipset")
  expect(html).toContain("Open this site on the client device")
  expect(html).toContain('href="/requests-log"')
  expect(html).not.toContain("overview.routingDiagnostics")
  expect(html).not.toContain("<table")
})

test("successful and unknown routes do not claim DNS is pending", () => {
  const success = render({
    ...diagnostics,
    results: [{ ...result, actual_outbound: "vpn", ok: true }],
  })
  expect(success).toContain("The route matches the rule")
  expect(success).not.toContain("The device’s DNS query may not have passed")
  const unknown = render({
    ...diagnostics,
    results: [{ ...result, actual_outbound: "(unknown)" }],
  })
  // The set check still answered "not in the set": that is a missing set
  // with the domain advice, not a failed check.
  expect(unknown).toContain("IP not in the expected ipset")
  expect(unknown).not.toContain("could not be checked")
  expect(unknown).toContain("Open this site on the client device")
})

test("unavailable outbound is reported separately from a matching firewall route", () => {
  const html = renderToStaticMarkup(
    <I18nextProvider i18n={i18n}>
      <RoutingRouteSummary
        diagnostics={{
          ...diagnostics,
          results: [{ ...result, actual_outbound: "vpn", ok: true }],
        }}
        runtimeOutbounds={[
          {
            tag: "vpn",
            type: "urltest",
            status: "unavailable",
            interfaces: [],
          },
        ]}
      />
    </I18nextProvider>
  )
  expect(html).toContain("The route matches the rule")
  expect(html).toContain("outbound vpn as unavailable")
})

test("a missing static IP-list entry is not blamed on DNS propagation", () => {
  const html = render({
    ...diagnostics,
    results: [{ ...result, list_match: { list: "static", via: result.ip } }],
  })
  expect(html).toContain("IP not in the expected ipset")
  expect(html).not.toContain("Open this site on the client device")
  expect(html).toContain(
    "Check applied configuration and list updates, then try again."
  )
})

test("multiple IPs share one trace and one failed route makes firewall step warn", () => {
  const secondIp = "5.6.7.8"
  const html = render({
    ...diagnostics,
    resolved_ips: [result.ip, secondIp],
    results: [
      { ...result, actual_outbound: "vpn", ok: true },
      { ...result, ip: secondIp },
    ],
    rule_diagnostics: [
      {
        ...rule,
        ip_rows: [...rule.ip_rows, { ...rule.ip_rows[0], ip: secondIp }],
      },
    ],
  })
  expect(html.match(/data-testid="routing-trace"/g)).toHaveLength(1)
  expect(html.match(/google \(entry <code/g)).toHaveLength(1)
  expect(html).toContain('data-step="1" data-tone="ok"')
  expect(html).toContain('data-step="3" data-tone="ok"')
  expect(html).toContain('data-step="4" data-tone="warn"')
  expect(stripTags(html)).toContain("Matching routes: 1 of 2")
  expect(html).toContain(secondIp)
})

test("route trace keeps IPs in DNS and groups firewall outcomes without repeating addresses", () => {
  const secondIp = "5.6.7.8"
  const html = render({
    ...diagnostics,
    resolved_ips: [result.ip, secondIp],
    results: [
      { ...result, actual_outbound: "wan", ok: false },
      { ...result, ip: secondIp, actual_outbound: "wan", ok: false },
    ],
    rule_diagnostics: [
      {
        ...rule,
        ip_rows: [...rule.ip_rows, { ...rule.ip_rows[0], ip: secondIp }],
      },
    ],
  })
  const firewall = html.split(">Firewall check<")[1]
  expect(firewall).toContain("Expected Outbound:")
  expect(firewall).toContain("Actual Outbound:")
  // Both IPs mismatch: the headline counts real matches, and a group that
  // covers every IP needs no "for N of M IPs" label.
  expect(stripTags(firewall)).toContain("Matching routes: 0 of 2")
  expect(stripTags(firewall)).not.toContain("Matching routes: 2 of 2")
  expect(firewall).not.toContain("For 2 of 2 IPs")
  // Visible text does not repeat the addresses; they sit behind the counter.
  expect(stripTags(firewall)).not.toContain(result.ip)
  expect(firewall).toContain('data-testid="ip-results"')
  expect(firewall).toContain(`aria-label="${result.ip} ⚠, ${secondIp} ⚠"`)
  expect(stripTags(firewall)).not.toContain(secondIp)
  expect(html.match(/google \(entry <code/g)).toHaveLength(1)
  expect(html).toContain(result.ip)
  expect(html).toContain(secondIp)
})

test("an uncertain rule for one IP warns at rule selection without blaming DNS", () => {
  const html = render({
    ...diagnostics,
    results: [
      { ...result, actual_outbound: "vpn", ok: true },
      {
        ...result,
        ip: "2001:db8::1",
        expected_outbound: "(unknown)",
        actual_outbound: "(unknown)",
      },
    ],
    resolved_ips: [result.ip, "2001:db8::1"],
  })
  expect(html).toContain('data-step="1" data-tone="ok"')
  expect(html).toContain('data-step="3" data-tone="warn"')
  expect(html).toContain('data-step="4" data-tone="warn"')
})

test("a retained successful operation is distinct from current kernel absence", () => {
  const html = render({
    ...diagnostics,
    rule_diagnostics: [
      {
        ...rule,
        ip_rows: [
          {
            ...rule.ip_rows[0],
            set_write_evidence: { status: "recorded", age_seconds: 0 },
          },
        ],
      },
    ],
  })
  expect(html).toContain(
    "keen-pbr has already added or refreshed this IP in the ipset."
  )
  expect(html).toContain("Last time 0 s ago.")
  expect(html).toContain("It is not in the ipset now")
})

test("no retained operation is not presented as proof that an IP was never added", () => {
  const html = render({
    ...diagnostics,
    rule_diagnostics: [
      {
        ...rule,
        ip_rows: [
          { ...rule.ip_rows[0], set_write_evidence: { status: "no_record" } },
        ],
      },
    ],
  })
  expect(html).toContain(
    "keen-pbr has no record of adding this IP to the ipset."
  )
  expect(html).not.toContain("Last time")
})

test("untracked history remains distinct from an empty tracked cache", () => {
  const html = render({
    ...diagnostics,
    rule_diagnostics: [
      {
        ...rule,
        ip_rows: [
          { ...rule.ip_rows[0], set_write_evidence: { status: "not_tracked" } },
        ],
      },
    ],
  })
  expect(html).toContain("Writes are not tracked for this ipset.")
  expect(html).not.toContain("keen-pbr has no record of adding")
})

const v6 = "2001:db8::1"
const twoFamilies: RoutingTestResponse = {
  ...diagnostics,
  resolved_ips: [result.ip, v6],
  results: [
    { ...result, actual_outbound: "vpn", ok: true },
    { ...result, ip: v6, actual_outbound: "(unknown)", ok: false },
  ],
  rule_diagnostics: [
    {
      ...rule,
      ip_rows: [
        { ...rule.ip_rows[0], in_ipset: true },
        { ...rule.ip_rows[0], ip: v6, in_ipset: undefined },
      ],
    },
  ],
}

test("IPv4 and IPv6 hitting one rule share a single rule block", () => {
  const html = render(twoFamilies)
  const ruleStep =
    html.split(">Firewall check<")[0].split(">Rule selection<")[1] ?? ""
  expect(ruleStep.match(/Rule #3/g) ?? []).toHaveLength(1)
  expect(ruleStep).not.toContain('data-testid="ip-group"')
})

test("a partial IP group shows a counter whose label lists the addresses", () => {
  const third = "9.9.9.9"
  const html = render({
    ...twoFamilies,
    resolved_ips: [result.ip, v6, third],
    results: [
      ...twoFamilies.results,
      { ...result, ip: third, actual_outbound: "wan", ok: false },
    ],
  })
  expect(html).toContain('data-testid="ip-group"')
  expect(html).toContain("cursor-help")
  expect(html).toContain("decoration-dashed")
  expect(html).toContain("2 of 3 addresses: ")
  expect(html).toContain(`${result.ip}, ${v6}`)
})

test("a group with a single IP shows the IP instead of a counter", () => {
  const html = render(twoFamilies)
  const firewall = html.split(">Firewall check<")[1]
  expect(firewall).not.toContain('data-testid="ip-group"')
  expect(firewall).toContain(`>${v6}</code>`)
  expect(firewall).toContain(`>${result.ip}</code>`)
})

test("IPv6 off: addresses are listed muted, noted and left out of the counts", () => {
  const html = render({
    ...twoFamilies,
    ipv6_enabled: false,
    results: [twoFamilies.results[0]],
    rule_diagnostics: [
      { ...rule, ip_rows: [twoFamilies.rule_diagnostics[0].ip_rows[0]] },
    ],
  })
  const dns = html.split(">Firewall check<")[0]
  expect(dns).toContain(v6)
  expect(dns).toContain("opacity-60")
  expect(html).toContain('data-testid="ipv6-disabled-note"')
  expect(html).toContain('href="/general#ipv6-enabled"')
  expect(html).toContain("IPv6 support is disabled")
  expect(stripTags(html)).toContain("Matching routes: 1 of 1")
  expect(html).not.toContain('data-testid="ip-group"')
})

test("a literal IPv6 target with IPv6 off is explained, not reported as unknown", () => {
  const html = render({
    ...diagnostics,
    target: v6,
    is_domain: false,
    resolved_ips: [],
    ipv6_enabled: false,
    results: [],
    rule_diagnostics: [{ ...rule, ip_rows: [] }],
  })
  expect(html).toContain("IPv6 is turned off")
  expect(html).toContain('data-testid="ipv6-disabled-note"')
  expect(html).not.toContain("Route problems detected")
})
