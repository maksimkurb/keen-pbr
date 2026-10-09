import { expect, test } from "bun:test"
import { splitListSourceUrl } from "../src/pages/lists-utils"

test("separates domain from the path, query and fragment", () => {
  expect(
    splitListSourceUrl("https://example.org:8443/lists/domains.txt?v=2#latest")
  ).toEqual({
    domain: "example.org:8443",
    remainder: "/lists/domains.txt?v=2#latest",
  })
})

test("keeps the domain for root URLs and safely handles invalid input", () => {
  expect(splitListSourceUrl("https://example.org")).toEqual({
    domain: "example.org",
    remainder: "/",
  })
  expect(splitListSourceUrl("invalid URL")).toEqual({
    domain: "invalid URL",
    remainder: "",
  })
})
