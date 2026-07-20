import { describe, expect, test } from "bun:test"

import { getInterfaceSearchText } from "../src/lib/runtime-interfaces"

describe("getInterfaceSearchText", () => {
  test("includes the optional Keenetic description", () => {
    expect(
      getInterfaceSearchText({
        name: "eth3",
        description: "Internet",
        status: "up",
      })
    ).toContain("Internet")
  })
})
