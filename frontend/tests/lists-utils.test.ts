import { describe, expect, test } from "bun:test"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import {
  buildUpdatedConfigForListDelete,
  getListDeleteImpact,
} from "../src/pages/lists-utils"

const config: ConfigObject = {
  lists: {
    removed: { ip_cidrs: ["203.0.113.1"] },
    retained: { ip_cidrs: ["203.0.113.2"] },
    sameNameDifferentId: { ip_cidrs: ["203.0.113.3"] },
  },
  route: {
    rules: [
      { outbound: "wan", list: ["retained"] },
      { outbound: "vpn", list: ["removed", "retained"] },
      { outbound: "direct", list: ["removed"], dest_port: "443" },
      { outbound: "drop", list: ["removed"] },
    ],
  },
}

describe("list deletion draft transformation", () => {
  test("only removes the selected list and preserves unrelated rules", () => {
    const updated = buildUpdatedConfigForListDelete(config, "removed")

    expect(updated.lists).toEqual({
      retained: config.lists?.retained,
      sameNameDifferentId: config.lists?.sameNameDifferentId,
    })
    expect(updated.route?.rules).toEqual([
      { outbound: "wan", list: ["retained"] },
      { outbound: "vpn", list: ["retained"] },
      { outbound: "direct", list: [], dest_port: "443" },
    ])
    expect(config.route?.rules?.[2]?.list).toEqual(["removed"])
  })

  test("reports only rules that lose their final condition", () => {
    expect(getListDeleteImpact(config, ["removed"])).toEqual({
      routeRuleIndexes: [1, 2, 3],
      removedRouteRuleIndexes: [3],
    })
  })
})
