import { describe, expect, test } from "bun:test"

import type { ConfigObject } from "../src/api/generated/model/configObject"

Object.defineProperty(globalThis, "navigator", {
  configurable: true,
  value: { language: "en-US", languages: ["en-US"] },
})

describe("a new DNS rule", () => {
  test("starts with no DNS server chosen, however many there are", async () => {
    const { newRuleDraft } = await import("../src/pages/dns-rules-utils")
    expect(newRuleDraft()).toEqual({
      enabled: true,
      servers: [],
      list: [],
      allow_domain_rebinding: false,
    })
  })

  test("the form starts from that draft", async () => {
    const source = await Bun.file(
      new URL("../src/pages/dns-rule-upsert-page.tsx", import.meta.url)
    ).text()
    expect(source).toContain("newRuleDraft()")
    expect(source).not.toContain("serverTags.slice")
  })

  test("cannot be saved until the user picks a server", async () => {
    await import("../src/i18n")
    const { newRuleDraft, validateRules } =
      await import("../src/pages/dns-rules-utils")
    const errors = validateRules(
      [{ ...newRuleDraft(), list: ["ads"] }],
      ["a", "b"],
      ["ads"]
    )
    expect(errors[0]?.servers).toBeDefined()

    expect(
      validateRules(
        [{ ...newRuleDraft(), list: ["ads"], servers: ["b"] }],
        ["a", "b"],
        ["ads"]
      )
    ).toEqual({})
  })

  test("saving rules leaves the main DNS servers as they were", async () => {
    const { buildUpdatedConfigWithRules } =
      await import("../src/pages/dns-rules-utils")
    const config: ConfigObject = { dns: { servers: [{ tag: "a" }] } }
    const saved = buildUpdatedConfigWithRules(config, [])
    expect("fallback" in (saved.dns ?? {})).toBe(false)
    expect("bootstrap" in (saved.dns ?? {})).toBe(false)
  })
})
