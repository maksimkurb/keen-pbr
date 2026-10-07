import { describe, expect, test } from "bun:test"
import type { AnyFormApi } from "@tanstack/react-form"

import {
  clearFieldServerErrors,
  clearFormServerErrors,
  clearServerErrorsOnChange,
  getFieldError,
  getServerFieldError,
  setFormServerErrors,
} from "../src/lib/form-api-errors"

const newForm = () => ({}) as AnyFormApi
const field = (form: AnyFormApi, name: string, errors: unknown[] = []) => ({
  form,
  name,
  state: { meta: { errors } },
})

describe("server errors by field name", () => {
  test("reach any field by name, mounted or not", () => {
    const form = newForm()
    setFormServerErrors(form, {
      fields: { "outboundGroups[0].members[1].target": "target required" },
    })
    expect(
      getFieldError(field(form, "outboundGroups[0].members[1].target"))
    ).toBe("target required")
    expect(
      getFieldError(field(form, "outboundGroups[0].members[0].target"))
    ).toBe(null)
  })

  test("a field's own validation error wins over the server one", () => {
    const form = newForm()
    setFormServerErrors(form, { fields: { tag: "server says no" } })
    expect(getFieldError(field(form, "tag", ["required"]))).toBe("required")
    expect(getFieldError(field(form, "tag", [undefined]))).toBe(
      "server says no"
    )
  })

  test("editing a field clears its errors and those below it only", () => {
    const form = newForm()
    setFormServerErrors(form, {
      fields: {
        outboundGroups: "group",
        "outboundGroups[0].members[0].weight": "weight",
        outboundGroupsExtra: "other field",
        tag: "tag",
      },
    })
    clearServerErrorsOnChange.onChange({
      fieldApi: { form, name: "outboundGroups" },
    })
    expect(getServerFieldError(form, "outboundGroups")).toBe(null)
    expect(
      getServerFieldError(form, "outboundGroups[0].members[0].weight")
    ).toBe(null)
    expect(getServerFieldError(form, "outboundGroupsExtra")).toBe("other field")
    expect(getServerFieldError(form, "tag")).toBe("tag")
    clearFieldServerErrors(form, "tag")
    expect(getServerFieldError(form, "tag")).toBe(null)
  })

  test("errors are per form and cleared on resubmit", () => {
    const first = newForm()
    const second = newForm()
    setFormServerErrors(first, { fields: { tag: "first" } })
    expect(getServerFieldError(second, "tag")).toBe(null)
    clearFormServerErrors(first)
    expect(getServerFieldError(first, "tag")).toBe(null)
  })
})
