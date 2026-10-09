import { expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"
import {
  buildRoutingTestRequest,
  initialRoutingTestCriteria,
} from "../src/components/overview/routing-test-criteria"
import { RoutingTestCriteriaFields } from "../src/components/overview/routing-test-criteria-fields"

test("routing tests default to TCP port 443 without unspecified criteria", () => {
  expect(
    buildRoutingTestRequest("google.com", initialRoutingTestCriteria)
  ).toEqual({ target: "google.com", proto: "tcp", dest_port: 443 })
  const html = renderToStaticMarkup(
    <RoutingTestCriteriaFields
      value={initialRoutingTestCriteria}
      onChange={() => {}}
    />
  )
  expect(html).toContain("TCP")
  expect(html).toContain('value="443"')
  expect(html).toContain('data-slot="collapsible-trigger"')
  expect(html).toContain('aria-expanded="false"')
  expect(html).not.toContain('id="routing-test-source-ip"')
})

test("additional packet criteria preserve DSCP zero and trim source IP", () => {
  expect(
    buildRoutingTestRequest("google.com", {
      ...initialRoutingTestCriteria,
      proto: "udp",
      src_addr: " 2001:db8::1 ",
      src_port: "53000",
      dscp: "0",
    })
  ).toEqual({
    target: "google.com",
    proto: "udp",
    dest_port: 443,
    src_addr: "2001:db8::1",
    src_port: 53000,
    dscp: 0,
  })
})

test("non-TCP/UDP packets omit both port fields", () => {
  expect(
    buildRoutingTestRequest("google.com", {
      ...initialRoutingTestCriteria,
      proto: "other",
      src_port: "1234",
    })
  ).toEqual({ target: "google.com", proto: "other" })
})
