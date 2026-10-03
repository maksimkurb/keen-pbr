import { describe, expect, test } from "bun:test"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import { isConfigMutationPending } from "../src/api/mutations"
import {
  pruneSelectedIds,
  selectVisibleIds,
  toggleSelectedId,
} from "../src/hooks/use-row-selection"
import { buildUpdatedConfigForDnsServersDelete } from "../src/pages/dns-servers-utils"
import {
  buildUpdatedConfigForListsDelete,
  listDeletesAltersRoutingRefs,
} from "../src/pages/lists-utils"

describe("row selection helpers", () => {
  test("prunes ids that are no longer visible", () => {
    expect([...pruneSelectedIds(["a", "missing", "b"], ["a", "b"])]).toEqual([
      "a",
      "b",
    ])
  })

  test("toggles one id after pruning stale ids", () => {
    expect([...toggleSelectedId(["a", "missing"], ["a", "b"], "b")]).toEqual([
      "a",
      "b",
    ])
    expect([...toggleSelectedId(["a", "missing"], ["a", "b"], "a")]).toEqual([])
  })

  test("selects and clears visible ids", () => {
    expect([...selectVisibleIds(["a", "b"], true)]).toEqual(["a", "b"])
    expect([...selectVisibleIds(["a", "b"], false)]).toEqual([])
  })
})

describe("config mutation pending helper", () => {
  test("is false with no mutations in flight", () => {
    expect(isConfigMutationPending(0, 0)).toBe(false)
  })

  test("is true with draft, apply, or discard mutations in flight", () => {
    expect(isConfigMutationPending(1, 0)).toBe(true)
    expect(isConfigMutationPending(0, 1)).toBe(true)
    expect(isConfigMutationPending(0, 0, 1)).toBe(true)
  })
})

describe("bulk DNS server delete helpers", () => {
  test("deletes the selected servers and keeps the rest", () => {
    const config: ConfigObject = {
      dns: {
        servers: [{ tag: "wan_dns" }, { tag: "vpn_dns" }],
      },
    }

    expect(buildUpdatedConfigForDnsServersDelete(config, ["wan_dns"])).toEqual({
      dns: {
        servers: [{ tag: "vpn_dns" }],
      },
    })
  })
})

describe("bulk list delete helpers", () => {
  test("deletes lists and reports changed routing refs", () => {
    const config: ConfigObject = {
      lists: {
        ads: { domains: ["ads.example"] },
        work: { domains: ["work.example"] },
      },
      route: {
        rules: [
          { list: ["ads", "work"], outbound: "vpn" },
          { list: ["ads"], outbound: "wan" },
        ],
      },
    }

    const nextConfig = buildUpdatedConfigForListsDelete(config, ["ads"])

    expect(Object.keys(nextConfig.lists ?? {})).toEqual(["work"])
    expect(nextConfig.route?.rules).toEqual([
      { list: ["work"], outbound: "vpn" },
    ])
    expect(listDeletesAltersRoutingRefs(config, nextConfig)).toBe(true)
  })
})
