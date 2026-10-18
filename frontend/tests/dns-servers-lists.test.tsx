import { describe, expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import {
  buildUpdatedConfigForDnsServersDelete,
  buildUpdatedConfigWithServerList,
  getBootstrapCandidateTags,
  getDnsServerDeleteImpact,
  parseServerIps,
} from "../src/pages/dns-servers-utils"

// The page texts come from the translations, in English here.
Object.defineProperty(globalThis, "navigator", {
  configurable: true,
  value: { language: "en-US", languages: ["en-US"] },
})

async function renderLists(props: {
  bootstrap?: string[]
  bootstrapOptions: string[]
  fallback?: string[]
  serverTags: string[]
}) {
  await import("../src/i18n")
  const { DnsServerLists } = await import("../src/pages/dns-servers-lists")
  return renderToStaticMarkup(
    <DnsServerLists
      bootstrap={props.bootstrap ?? []}
      bootstrapOptions={props.bootstrapOptions}
      fallback={props.fallback ?? []}
      onBootstrapChange={() => {}}
      onFallbackChange={() => {}}
      serverTags={props.serverTags}
    />
  )
}

describe("the DNS servers page lists", () => {
  test("shows the main servers and the bootstrap servers, in that order", async () => {
    const html = await renderLists({
      bootstrapOptions: ["a"],
      fallback: ["a"],
      serverTags: ["a", "g"],
    })
    expect(html).toContain("Main DNS servers")
    expect(html).toContain("Bootstrap servers")
    expect(html.indexOf("Main DNS servers")).toBeLessThan(
      html.indexOf("Bootstrap servers")
    )
    expect(html).toContain(
      "keen-pbr uses these servers to find the IP addresses of DoH servers"
    )
  })

  test("with no bootstrap server chosen it names the automatic one", async () => {
    const html = await renderLists({
      bootstrapOptions: ["a", "b"],
      serverTags: ["a", "b", "g"],
    })
    expect(html).toContain("Automatic: a")
  })

  test("without plain servers it names the system resolver and says what to do", async () => {
    const html = await renderLists({
      bootstrapOptions: [],
      serverTags: ["g"],
    })
    expect(html).toContain("The router&#x27;s system resolver")
    expect(html).toContain("Add a plain DNS server")
  })

  test("the DNS rules page no longer edits the main servers", async () => {
    const source = await Bun.file(
      new URL("../src/pages/dns-rules-page.tsx", import.meta.url)
    ).text()
    expect(source).not.toContain("fallback")
    expect(source).not.toContain("MultiSelectList")
  })
})

describe("bootstrap and main server lists in the config", () => {
  const config: ConfigObject = {
    dns: {
      servers: [
        { tag: "a" },
        { tag: "k", type: "keenetic" },
        { tag: "g", type: "doh", address: "https://dns.google/dns-query" },
      ],
      fallback: ["g"],
    },
  }

  test("only plain servers can be bootstrap servers", () => {
    expect(getBootstrapCandidateTags(config.dns?.servers ?? [])).toEqual([
      "a",
      "k",
    ])
  })

  test("a chosen list is written, an empty one is left out", () => {
    const withBootstrap = buildUpdatedConfigWithServerList(
      config,
      "bootstrap",
      ["k", "a"]
    )
    expect(withBootstrap.dns?.bootstrap).toEqual(["k", "a"])
    expect(withBootstrap.dns?.fallback).toEqual(["g"])

    const cleared = buildUpdatedConfigWithServerList(
      withBootstrap,
      "bootstrap",
      []
    )
    expect("bootstrap" in (cleared.dns ?? {})).toBe(false)

    const noFallback = buildUpdatedConfigWithServerList(config, "fallback", [])
    expect("fallback" in (noFallback.dns ?? {})).toBe(false)
    expect(noFallback.dns?.servers).toEqual(config.dns?.servers)
  })

  test("deleting a server cleans the main and the bootstrap lists", () => {
    const withBootstrap: ConfigObject = {
      dns: { ...config.dns, bootstrap: ["a", "k"] },
    }
    expect(getDnsServerDeleteImpact(withBootstrap, ["a"])).toEqual({
      matchingRuleIndexes: [],
      usesFallback: false,
      usesBootstrap: true,
    })

    const updated = buildUpdatedConfigForDnsServersDelete(
      withBootstrap,
      ["a"],
      true
    )
    expect(updated.dns?.bootstrap).toEqual(["k"])

    const last = buildUpdatedConfigForDnsServersDelete(updated, ["k"], true)
    expect("bootstrap" in (last.dns ?? {})).toBe(false)

    const doh = buildUpdatedConfigForDnsServersDelete(
      withBootstrap,
      ["g"],
      true
    )
    expect(doh.dns?.fallback).toEqual([])
    expect(doh.dns?.bootstrap).toEqual(["a", "k"])
  })

  test("deleting a server leaves a config without bootstrap servers alone", () => {
    const updated = buildUpdatedConfigForDnsServersDelete(config, ["a"], true)
    expect("bootstrap" in (updated.dns ?? {})).toBe(false)
  })
})

describe("the IP addresses of a DoH server", () => {
  test("a comma separated list of addresses is read", () => {
    expect(parseServerIps("8.8.8.8, 8.8.4.4")).toEqual({
      ips: ["8.8.8.8", "8.8.4.4"],
      invalid: false,
    })
    expect(parseServerIps("2001:4860:4860::8888")).toEqual({
      ips: ["2001:4860:4860::8888"],
      invalid: false,
    })
  })

  test("an empty field means no addresses", () => {
    expect(parseServerIps("  ")).toEqual({ ips: [], invalid: false })
  })

  test("names, ports and repeats are refused", () => {
    expect(parseServerIps("dns.google").invalid).toBe(true)
    expect(parseServerIps("8.8.8.8:443").invalid).toBe(true)
    expect(parseServerIps("8.8.8.8, 8.8.8.8").invalid).toBe(true)
    expect(parseServerIps("8.8.8.256").invalid).toBe(true)
  })
})
