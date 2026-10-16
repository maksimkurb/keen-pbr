import { expect, test } from "bun:test"
import { validateListSource } from "../src/lib/list-source-validation"

const empty = { source: "", url: "", file: "", domains: "", ip_cidrs: "" }
const t = (key: string) => key

test("selected URL/file sources require non-whitespace content", () => {
  for (const source of ["url", "file"] as const) {
    expect(
      validateListSource({ ...empty, source, [source]: " \n " }, t)
    ).toEqual({ [source]: "common.validation.required" })
    expect(
      validateListSource({ ...empty, source, [source]: "source" }, t)
    ).toEqual({})
  }
})

test("inline content accepts either domains or IP/CIDR; whitespace is empty", () => {
  expect(
    validateListSource({ ...empty, source: "inline", domains: " \n" }, t)
  ).toEqual({
    domains: "pages.listUpsert.validation.inlineRequired",
    ip_cidrs: "pages.listUpsert.validation.inlineRequired",
  })
  for (const field of ["domains", "ip_cidrs"]) {
    expect(
      validateListSource({ ...empty, source: "inline", [field]: "entry" }, t)
    ).toEqual({})
  }
})

test("legacy combined sources are preserved without requiring each source", () => {
  expect(
    validateListSource(
      { ...empty, url: "https://example.com/list", domains: "example.com" },
      t
    )
  ).toEqual({})
})
