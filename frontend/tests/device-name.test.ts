import { describe, expect, test } from "bun:test"

import { getDevicePageTitle } from "../src/auth/device-name"

describe("device page title", () => {
  test("uses the product name when the device name is empty", () => {
    expect(getDevicePageTitle("")).toBe("keen-pbr")
  })

  test("prefixes the product name with the configured device name", () => {
    expect(getDevicePageTitle("Home router")).toBe("Home router - keen-pbr")
  })
})
