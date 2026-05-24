import { describe, expect, test } from "bun:test"

import { buildListUsageByName, getRulesUsingList } from "../src/lib/list-usage"

describe("list usage helpers", () => {
  const rules = [
    { list: ["shared", "ads"], target: "wan" },
    { list: ["shared"], target: "vpn" },
    { list: [], target: "direct" },
  ]

  test("finds rules that use a list without formatting UI text", () => {
    expect(
      getRulesUsingList(
        "shared",
        rules,
        (rule) => rule.list,
        (rule) => rule.target
      )
    ).toEqual([
      { ruleIndex: 0, target: "wan" },
      { ruleIndex: 1, target: "vpn" },
    ])
  })

  test("excludes the edited rule when requested", () => {
    expect(
      getRulesUsingList(
        "shared",
        rules,
        (rule) => rule.list,
        (rule) => rule.target,
        0
      )
    ).toEqual([{ ruleIndex: 1, target: "vpn" }])
  })

  test("builds a reusable list-name map", () => {
    const usageByName = buildListUsageByName(
      rules,
      (rule) => rule.list,
      (rule) => rule.target
    )

    expect(usageByName.get("shared")).toEqual([
      { ruleIndex: 0, target: "wan" },
      { ruleIndex: 1, target: "vpn" },
    ])
    expect(usageByName.get("ads")).toEqual([{ ruleIndex: 0, target: "wan" }])
  })
})
