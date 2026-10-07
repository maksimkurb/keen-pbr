import { describe, expect, test } from "bun:test"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import { getIn } from "../src/lib/draft-form-core"
import {
  buildUpdatedConfig,
  getDraftFromConfig,
} from "../src/pages/general-config-page"

const config = {
  device_name: "router",
  dns: {},
  daemon: {
    strict_enforcement: false,
    skip_marked_packets: true,
    clear_dynamic_sets_on_apply: true,
    ipv6_enabled: false,
    ipset_hashsize: 2048,
    ipset_maxelem: 70000,
  },
  route: { inbound_interfaces: ["br0", "br1"] },
  lists_autoupdate: { enabled: true, cron: "0 5 * * *" },
  fwmark: { start: "0x00020000", mask: "0x00ff0000" },
  iproute: { table_start: 200, process_router_traffic: true },
  intercept: {
    enabled: true,
    min_ttl_s: 10,
    max_ttl_s: 20,
    dns: {
      enabled: false,
      queue_num: 1,
      hold_timeout_ms: 2,
      marker: { domain: "m.test", answer_ipv4: "127.0.0.9" },
    },
    l7: { enabled: false, nflog_group: 3, tls: false, http: true, quic: false },
  },
} as unknown as ConfigObject

describe("settings draft mirrors the config document", () => {
  test("a draft path is the config path of the value", () => {
    const draft = getDraftFromConfig(config)
    for (const path of [
      "device_name",
      "daemon.strict_enforcement",
      "daemon.ipv6_enabled",
      "route.inbound_interfaces",
      "lists_autoupdate.cron",
      "fwmark.start",
      "iproute.process_router_traffic",
      "intercept.dns.marker.answer_ipv4",
      "intercept.l7.quic",
    ]) {
      expect(getIn(draft, path)).toEqual(getIn(config, path))
    }
    // numbers are edited as text
    expect(getIn(draft, "daemon.ipset_hashsize")).toBe("2048")
    expect(getIn(draft, "iproute.table_start")).toBe("200")
  })

  test("saving an untouched draft keeps the config", () => {
    expect(buildUpdatedConfig(config, getDraftFromConfig(config))).toEqual(
      config
    )
  })
})
