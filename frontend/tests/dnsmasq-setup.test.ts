import { describe, expect, test } from "bun:test"

import {
  dnsmasqInstallCommand,
  dnsmasqSetupDocsUrl,
  dnsmasqSetupOsLabel,
  isDnsmasqSetupError,
} from "../src/lib/dnsmasq-setup"

const DOCS = "https://keen-pbr.fyi/ru/docs/"

describe("dnsmasq setup help", () => {
  test("install command follows the OS and its package manager", () => {
    expect(dnsmasqInstallCommand("keenetic", "4.3.1")).toBe(
      "opkg install dnsmasq-full"
    )
    expect(dnsmasqInstallCommand("debian", "12")).toBe("apt install dnsmasq")
    expect(dnsmasqInstallCommand("openwrt", "24.10.4")).toBe(
      "opkg install dnsmasq-full"
    )
    expect(dnsmasqInstallCommand("openwrt", "25.12.0")).toBe(
      "apk add dnsmasq-full"
    )
    expect(dnsmasqInstallCommand("openwrt", "SNAPSHOT")).toBe(
      "opkg install dnsmasq-full"
    )
    expect(dnsmasqInstallCommand("linux", "1")).toBeUndefined()
    expect(dnsmasqInstallCommand(undefined)).toBeUndefined()
  })

  test("docs link points at the OS install guide, else the generic section", () => {
    expect(dnsmasqSetupDocsUrl(DOCS, "keenetic")).toBe(
      "https://keen-pbr.fyi/ru/docs/getting-started/installation/keenetic/#dnsmasq-install"
    )
    expect(dnsmasqSetupDocsUrl(DOCS, "linux")).toBe(
      "https://keen-pbr.fyi/ru/docs/configuration/dns/#do-i-need-dnsmasq"
    )
    expect(dnsmasqSetupOsLabel("openwrt")).toBe("OpenWrt")
    expect(dnsmasqSetupOsLabel("linux")).toBeUndefined()
  })

  test("only install and setup problems get the instructions link", () => {
    expect(isDnsmasqSetupError("DNSMASQ_NOT_INSTALLED")).toBe(true)
    expect(isDnsmasqSetupError("DNSMASQ_DROPIN_NOT_LOADED")).toBe(true)
    expect(isDnsmasqSetupError("DNSMASQ_APPLY_FAILED")).toBe(false)
    expect(isDnsmasqSetupError(undefined)).toBe(false)
  })
})
