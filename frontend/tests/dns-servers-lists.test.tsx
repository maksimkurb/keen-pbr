import { describe, expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import {
  buildUpdatedConfigForDnsServersDelete,
  buildUpdatedConfigWithServerList,
  getBootstrapCandidateTags,
  getDnsServerDeleteImpact,
  getDnsServerListWarnings,
  getDnsServerRoles,
  parseServerIps,
} from "../src/pages/dns-servers-utils"

// The page texts come from the translations, in English here.
Object.defineProperty(globalThis, "navigator", {
  configurable: true,
  value: { language: "en-US", languages: ["en-US"], userAgent: "" },
})

async function renderFallback(props: {
  fallback?: string[]
  serverTags: string[]
}) {
  await import("../src/i18n")
  const { FallbackServerList } = await import("../src/pages/dns-servers-lists")
  return renderToStaticMarkup(
    <FallbackServerList
      fallback={props.fallback ?? []}
      onChange={() => {}}
      serverTags={props.serverTags}
    />
  )
}

async function renderBootstrap(props: {
  bootstrap?: string[]
  bootstrapOptions: string[]
  dohWithoutIps?: string[]
}) {
  await import("../src/i18n")
  const { BootstrapServerList } = await import("../src/pages/dns-servers-lists")
  return renderToStaticMarkup(
    <BootstrapServerList
      bootstrap={props.bootstrap ?? []}
      bootstrapOptions={props.bootstrapOptions}
      dohWithoutIps={props.dohWithoutIps ?? []}
      onChange={() => {}}
    />
  )
}

describe("the DNS servers page lists", () => {
  test("shows the main servers and the bootstrap servers", async () => {
    expect(
      await renderFallback({ fallback: ["a"], serverTags: ["a", "g"] })
    ).toContain("Add main DNS server")
    const html = await renderBootstrap({ bootstrapOptions: ["a"] })
    expect(html).toContain("Add bootstrap server")
    expect(html).toContain(
      "used to look up the IP addresses of the DoH upstream servers"
    )
  })

  test("the sections come in order: main servers, then bootstrap", async () => {
    await import("../src/i18n")
    const { DnsServerListsAccordion } =
      await import("../src/pages/dns-servers-lists")
    const html = renderToStaticMarkup(
      <DnsServerListsAccordion
        bootstrap={[]}
        bootstrapOptions={["a"]}
        dohWithoutIps={[]}
        fallback={["a"]}
        fallbackWarning={false}
        onBootstrapChange={() => {}}
        onFallbackChange={() => {}}
        serverTags={["a"]}
      />
    )
    expect(html.indexOf("Main DNS servers")).toBeGreaterThan(-1)
    expect(html.indexOf("Main DNS servers")).toBeLessThan(
      html.indexOf("Bootstrap servers")
    )
  })

  test("with no bootstrap server chosen it names the automatic one", async () => {
    const html = await renderBootstrap({ bootstrapOptions: ["a", "b"] })
    expect(html).toContain("Automatic: a")
  })

  test("without plain servers it names the system resolver and says what to do", async () => {
    const html = await renderBootstrap({ bootstrapOptions: [] })
    expect(html).toContain("The router&#x27;s system resolver")
    expect(html).toContain("Add a plain DNS server")
  })

  test("names the DoH servers that need bootstrap servers", async () => {
    const html = await renderBootstrap({
      bootstrapOptions: ["a"],
      dohWithoutIps: ["g"],
    })
    expect(html).toContain(
      "The DoH servers <strong>g</strong> have no IP address"
    )
  })

  test("collapsed sections say which servers are in use", async () => {
    await import("../src/i18n")
    const { DnsServerListsAccordion } =
      await import("../src/pages/dns-servers-lists")
    const render = (props: { fallback: string[]; bootstrap: string[] }) =>
      renderToStaticMarkup(
        <DnsServerListsAccordion
          bootstrapOptions={["a", "b"]}
          dohWithoutIps={[]}
          fallbackWarning={props.fallback.length === 0}
          onBootstrapChange={() => {}}
          onFallbackChange={() => {}}
          serverTags={["a", "b", "g"]}
          {...props}
        />
      )
    const chosen = render({ fallback: ["g", "a"], bootstrap: ["b"] })
    expect(chosen).toContain("g, a")
    expect(chosen).toContain(">b<")
    expect(chosen).not.toContain("Needs attention")
    const empty = render({ fallback: [], bootstrap: [] })
    expect(empty).toContain("No servers specified")
    expect(empty).toContain("Needs attention")
  })
})

describe("the warnings on the tabs of the DNS servers page", () => {
  const servers = [
    { tag: "a" },
    { tag: "g", type: "doh" as const, address: "https://dns.google/dns-query" },
    {
      tag: "p",
      type: "doh" as const,
      address: "https://dns.example/dns-query",
      ips: ["192.0.2.1"],
    },
    { tag: "c", type: "doh" as const, address: "https://1.1.1.1/dns-query" },
    {
      tag: "v6",
      type: "doh" as const,
      address: "https://[2606:4700::1111]/dns-query",
    },
  ]

  test("no main servers chosen", () => {
    expect(getDnsServerListWarnings({ dns: { servers } }).fallback).toBe(true)
    expect(
      getDnsServerListWarnings({ dns: { servers, fallback: ["a"] } }).fallback
    ).toBe(false)
  })

  test("a DoH server named by host, without IPs and bootstrap servers", () => {
    expect(getDnsServerListWarnings({ dns: { servers } })).toMatchObject({
      bootstrap: true,
      dohWithoutIps: ["g"],
    })
    expect(
      getDnsServerListWarnings({ dns: { servers, bootstrap: ["a"] } }).bootstrap
    ).toBe(false)
    expect(
      getDnsServerListWarnings({ dns: { servers: [servers[0], servers[2]] } })
        .bootstrap
    ).toBe(false)
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

describe("the roles shown under a server name", () => {
  test("a place is shown only in a list of several servers", () => {
    expect(getDnsServerRoles("a", ["a", "b"], ["a"], ["a"])).toEqual([
      { kind: "fallback", position: 1 },
      { kind: "bootstrap" },
    ])
    expect(getDnsServerRoles("b", ["a", "b"], [], [])).toEqual([
      { kind: "fallback", position: 2 },
    ])
  })

  test("with no bootstrap server chosen the first plain one is automatic", () => {
    expect(getDnsServerRoles("a", [], [], ["a", "b"])).toEqual([
      { kind: "bootstrap", auto: true },
    ])
    expect(getDnsServerRoles("b", [], [], ["a", "b"])).toEqual([])
    expect(getDnsServerRoles("a", [], ["b"], ["a", "b"])).toEqual([])
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

describe("the notification of a DNS check", () => {
  test("a well-known site reported missing says what it may mean", async () => {
    await import("../src/i18n")
    const { DnsCheckResultDetails } =
      await import("../src/pages/dns-servers-check")
    const html = renderToStaticMarkup(
      <DnsCheckResultDetails
        draft={false}
        summary={{
          level: "warning",
          errors: [],
          warnings: [
            {
              tag: "quad9",
              address: "9.9.9.9",
              status: "nxdomain",
              domain: "telegram.org",
            },
          ],
        }}
      />
    )
    expect(html).toContain(
      "<strong>quad9</strong>: The DNS server answered that the domain <strong>telegram.org does not exist</strong>. This may also mean that the DNS server or the ISP tampers with the answer."
    )
  })
})

describe("the notification of a check of the user's own site", () => {
  test("asks the user to judge whether the site exists", async () => {
    await import("../src/i18n")
    const { DnsCheckResultDetails } =
      await import("../src/pages/dns-servers-check")
    const html = renderToStaticMarkup(
      <DnsCheckResultDetails
        draft={false}
        summary={{
          level: "warning",
          errors: [],
          warnings: [
            {
              tag: "quad9",
              address: "9.9.9.9",
              status: "nxdomain",
              domain: "mine.example",
            },
          ],
        }}
      />
    )
    expect(html).toContain(
      "<strong>quad9</strong>: The DNS server answered that the domain <strong>mine.example does not exist</strong>. If you are sure this domain exists,"
    )
  })
})
