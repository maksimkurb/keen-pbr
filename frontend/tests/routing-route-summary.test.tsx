import { expect, test } from "bun:test"
import { createInstance } from "i18next"
import { I18nextProvider } from "react-i18next"
import { renderToStaticMarkup } from "react-dom/server"
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
  results: [result],
  rule_diagnostics: [rule],
}
function render(value: RoutingTestResponse) {
  return renderToStaticMarkup(
    <I18nextProvider i18n={i18n}>
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
              { outbound_tag: "wan", interface_name: "eth1", status: "active" },
            ],
          },
        ]}
      />
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
  expect(html).toContain("warp → vpn_cf")
  expect(html).not.toContain("vpn_backup")
  expect(html).toContain("wan → eth1")
  expect(html).not.toContain(
    "Check applied configuration and list updates, then try again."
  )
  expect(html).toContain("Step 1 of 4")
  expect(html).toContain("DNS resolution")
  expect(html).toContain("IP missing from the expected IPSet")
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
  expect(unknown).toContain("The actual route is unknown")
  expect(unknown).not.toContain("The device’s DNS query may not have passed")
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
  expect(html).toContain("IP missing from the expected IPSet")
  expect(html).not.toContain("The device’s DNS query may not have passed")
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
  expect(html).toContain("Matching routes: 1 of 2")
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
  expect(firewall).toContain("Matching routes: 2 of 2")
  expect(firewall).not.toContain(result.ip)
  expect(firewall).not.toContain(secondIp)
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
    "keen-pbr successfully added, refreshed or confirmed this IP in the set."
  )
  expect(html).toContain("Latest retained operation: 0 s ago.")
  expect(html).toContain(
    "A successful operation was recorded, but the IP is absent now."
  )
  expect(html).not.toContain("The device’s DNS query may not have passed")
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
    "No successful operation for this IP and set is retained in the cache."
  )
  expect(html).toContain(
    "missing history does not mean the IP was never added."
  )
  expect(html).not.toContain("Latest retained operation:")
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
  expect(html).toContain(
    "Dynamic write history for this set is currently unavailable."
  )
  expect(html).not.toContain(
    "No successful operation for this IP and set is retained"
  )
})
