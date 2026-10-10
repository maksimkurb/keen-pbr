import { expect, test } from "bun:test"
import type {
  RoutingTestEntry,
  RoutingTestResponse,
} from "../src/api/generated/model"
import { getRoutingDiagnosticIssue } from "../src/components/overview/routing-diagnostics-utils"

const result: RoutingTestEntry = {
  ip: "1.2.3.4",
  expected_outbound: "vpn",
  actual_outbound: "(default)",
  matched_rule_index: 2,
  ok: false,
}
const selectedRule = {
  rule_index: 2,
  rule: { outbound: "vpn", list: ["vpn"] },
  outbound: "vpn",
  interface_name: "vpn0",
  target_in_lists: true,
  ip_rows: [
    { ip: result.ip, in_lists: true, in_ipset: false, criteria_match: true },
  ],
}
const response: RoutingTestResponse = {
  target: "example.com",
  is_domain: true,
  resolved_ips: [result.ip],
  warnings: [],
  no_matching_rule: false,
  results: [result],
  rule_diagnostics: [selectedRule],
}
function classify(entry = result, rules = response.rule_diagnostics) {
  return getRoutingDiagnosticIssue(
    { ...response, rule_diagnostics: rules },
    entry
  )
}
test("no DNS answer takes precedence over synthetic unknown firewall result", () => {
  expect(
    getRoutingDiagnosticIssue(
      { ...response, resolved_ips: [] },
      { ...result, ip: "(no IPs resolved)", actual_outbound: "(unknown)" }
    )
  ).toBe("dns")
  expect(
    getRoutingDiagnosticIssue(
      { ...response, is_domain: false, resolved_ips: [] },
      result
    )
  ).toBe("missing_ipset")
})
test("a known missing set differs from an unavailable membership check", () => {
  expect(classify()).toBe("missing_ipset")
  expect(classify({ ...result, actual_outbound: "(unknown)" })).toBe("firewall")
  expect(
    classify(result, [
      {
        ...selectedRule,
        ip_rows: [{ ...selectedRule.ip_rows[0], in_ipset: null }],
      },
    ])
  ).toBe("mismatch")
})
test("other set membership must match the actual outbound and packet criteria", () => {
  const other = {
    ...selectedRule,
    rule_index: 1,
    outbound: "wan",
    rule: { outbound: "wan", list: ["wan"] },
    ip_rows: [{ ...selectedRule.ip_rows[0], in_ipset: true }],
  }
  expect(
    classify({ ...result, actual_outbound: "wan" }, [other, selectedRule])
  ).toBe("other_ipset")
  expect(
    classify({ ...result, actual_outbound: "wan" }, [
      { ...other, ip_rows: [{ ...other.ip_rows[0], criteria_match: false }] },
      selectedRule,
    ])
  ).toBe("missing_ipset")
  expect(
    classify({ ...result, actual_outbound: "wan" }, [
      other,
      {
        ...selectedRule,
        ip_rows: [{ ...selectedRule.ip_rows[0], in_ipset: true }],
      },
    ])
  ).toBe("conflicting_ipsets")
})
test("unexpected set membership is reported without inventing a selected rule", () => {
  const stale = {
    ...selectedRule,
    rule_index: 1,
    outbound: "wan",
    ip_rows: [{ ...selectedRule.ip_rows[0], in_ipset: true, in_lists: false }],
  }
  const noRule = {
    ...result,
    expected_outbound: "(default)",
    actual_outbound: "wan",
    matched_rule_index: undefined,
  }
  expect(classify(noRule, [stale])).toBe("stale_ipset")
  expect(
    classify(noRule, [{ ...stale, rule: { ...stale.rule, enabled: false } }])
  ).toBe("mismatch")
})
test("unknown criteria are not presented as DNS failure or a missing IPSet", () => {
  expect(
    classify({
      ...result,
      expected_outbound: "(unknown)",
      actual_outbound: "(unknown)",
    })
  ).toBe("criteria")
  expect(classify({ ...result, actual_outbound: "vpn", ok: true })).toBe("ok")
  expect(
    classify(
      {
        ...result,
        expected_outbound: "(default)",
        actual_outbound: "(default)",
        matched_rule_index: undefined,
        ok: true,
      },
      []
    )
  ).toBe("ok")
})
