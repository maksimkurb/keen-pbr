import { describe, expect, test } from "bun:test"

import type { ApiError } from "../src/api/client"
import {
  apiErrorsToServerErrors,
  createDraftStore,
  deepEqual,
  getIn,
  isAtOrBelow,
  normalizePath,
  parsePath,
  setIn,
  splitFormApiErrors,
} from "../src/lib/draft-form-core"

type Draft = {
  tag: string
  groups: Array<{ members: Array<{ target: string; weight: string }> }>
}

const draft = (): Draft => ({
  tag: "a",
  groups: [
    { members: [{ target: "t0", weight: "1" }] },
    {
      members: [
        { target: "t1", weight: "2" },
        { target: "t2", weight: "3" },
      ],
    },
  ],
})

const apiError = (details: unknown, message = "failed") =>
  ({ message, details }) as unknown as ApiError

describe("paths", () => {
  test("parse and normalize", () => {
    expect(parsePath("a.b[2].c")).toEqual(["a", "b", "2", "c"])
    expect(normalizePath("a.b.2.c")).toBe("a.b[2].c")
    expect(normalizePath("groups[0].members[1].weight")).toBe(
      "groups[0].members[1].weight"
    )
  })

  test("getIn reads nested arrays and tolerates missing branches", () => {
    const value = draft()
    expect(getIn(value, "groups[1].members[0].target")).toBe("t1")
    expect(getIn(value, "groups[5].members[0].target")).toBeUndefined()
    expect(getIn(value, "tag.x")).toBeUndefined()
  })

  test("setIn is immutable and shares untouched branches", () => {
    const value = draft()
    const next = setIn(value, "groups[1].members[0].weight", "9")
    expect(getIn(next, "groups[1].members[0].weight")).toBe("9")
    expect(getIn(value, "groups[1].members[0].weight")).toBe("2")
    expect(next.groups[0]).toBe(value.groups[0])
    expect(next.groups[1].members[1]).toBe(value.groups[1].members[1])
    expect(Array.isArray(next.groups)).toBe(true)
    expect(setIn(value, "tag", "a")).toBe(value)
  })

  test("setIn creates missing containers", () => {
    const next = setIn({} as Record<string, unknown>, "a[1].b", "x")
    expect(next).toEqual({ a: [undefined, { b: "x" }] })
    expect(Array.isArray(next.a)).toBe(true)
  })

  test("isAtOrBelow follows segments, not string prefixes", () => {
    expect(isAtOrBelow("a", "a")).toBe(true)
    expect(isAtOrBelow("a[0].b", "a")).toBe(true)
    expect(isAtOrBelow("a.b", "a")).toBe(true)
    expect(isAtOrBelow("aExtra", "a")).toBe(false)
    expect(isAtOrBelow("a[10]", "a[1]")).toBe(false)
    expect(isAtOrBelow("a", "a[0]")).toBe(false)
  })

  test("deepEqual", () => {
    expect(deepEqual(draft(), draft())).toBe(true)
    expect(
      deepEqual(draft(), setIn(draft(), "groups[0].members[0].weight", "7"))
    ).toBe(false)
    expect(deepEqual([1], { 0: 1 })).toBe(false)
  })
})

describe("dirty tracking", () => {
  test("is dirty only while values differ from the baseline", () => {
    const store = createDraftStore(draft())
    const dirty = () =>
      !deepEqual(store.getState().values, store.getState().baseline)
    expect(dirty()).toBe(false)
    store.setValue("groups[0].members[0].weight", "5")
    expect(dirty()).toBe(true)
    store.setValue("groups[0].members[0].weight", "1")
    expect(dirty()).toBe(false)
  })

  test("reset(next) makes next the new clean value", () => {
    const store = createDraftStore(draft())
    store.setValue("tag", "b")
    store.reset()
    expect(store.getState().values.tag).toBe("a")
    store.reset({ ...draft(), tag: "c" })
    expect(store.getState().values.tag).toBe("c")
    expect(store.getState().baseline.tag).toBe("c")
  })

  test("a changed initial is adopted only while clean", () => {
    const store = createDraftStore(draft())
    store.syncInitial({ ...draft(), tag: "server" })
    expect(store.getState().values.tag).toBe("server")
    store.setValue("tag", "mine")
    store.syncInitial({ ...draft(), tag: "server2" })
    expect(store.getState().values.tag).toBe("mine")
    store.syncInitial({ ...draft(), tag: "server2" })
    expect(store.getState().values.tag).toBe("mine")
  })

  test("consecutive updates compose", () => {
    const store = createDraftStore(draft())
    store.setValue("tag", "x")
    store.setValues((previous) => ({ ...previous, tag: `${previous.tag}y` }))
    expect(store.getState().values.tag).toBe("xy")
  })
})

describe("error clearing", () => {
  const withErrors = () => {
    const store = createDraftStore(draft())
    store.setServerErrors({
      form: "boom",
      fields: {
        groups: "group",
        "groups[0].members[0].weight": "w0",
        "groups[1].members[1].target": "t",
        groupsExtra: "other",
        tag: "tag",
      },
      unmapped: [{ path: "x", message: "y" }],
    })
    return store
  }

  test("editing a path clears it and everything below, never siblings", () => {
    const store = withErrors()
    store.setValue("groups", [])
    expect(store.errorFor("groups")).toBe(null)
    expect(store.errorFor("groups[0].members[0].weight")).toBe(null)
    expect(store.errorFor("groups[1].members[1].target")).toBe(null)
    expect(store.errorFor("groupsExtra")).toBe("other")
    expect(store.errorFor("tag")).toBe("tag")
  })

  test("editing one array row keeps other rows' errors", () => {
    const store = withErrors()
    store.setValue("groups[0].members[0].weight", "4")
    expect(store.errorFor("groups[0].members[0].weight")).toBe(null)
    expect(store.errorFor("groups[1].members[1].target")).toBe("t")
    expect(store.errorFor("groups")).toBe("group")
  })

  test("setValues clears errors of paths whose value changed only", () => {
    const store = withErrors()
    store.setValues((previous) => ({ ...previous, tag: "new" }))
    expect(store.errorFor("tag")).toBe(null)
    expect(store.errorFor("groups[0].members[0].weight")).toBe("w0")
    store.setValues((previous) => ({
      ...previous,
      groups: previous.groups.slice(0, 1),
    }))
    // the removed row's error is gone, the surviving row keeps its own
    expect(store.errorFor("groups[1].members[1].target")).toBe(null)
    expect(store.errorFor("groups[0].members[0].weight")).toBe("w0")
  })

  test("form and unmapped errors stay until cleared or resubmitted", () => {
    const store = withErrors()
    store.setValue("tag", "z")
    expect(store.getState().errors.form).toBe("boom")
    expect(store.getState().errors.unmapped).toHaveLength(1)
    store.clearServerErrors()
    expect(store.getState().errors.form).toBe(null)
    expect(store.errorFor("groupsExtra")).toBe(null)
  })

  test("paths are matched in any spelling", () => {
    const store = createDraftStore(draft())
    store.setServerErrors({ fields: { "groups.0.members.1.target": "bad" } })
    expect(store.errorFor("groups[0].members[1].target")).toBe("bad")
  })
})

describe("submit", () => {
  test("clears errors, passes the values and tracks isSubmitting", async () => {
    const store = createDraftStore(draft())
    store.setServerErrors({ fields: { tag: "old" }, form: "old" })
    const seen: boolean[] = []
    await store.submit(async (values) => {
      seen.push(store.getState().isSubmitting)
      expect(values.tag).toBe("a")
      expect(store.getState().errors.form).toBe(null)
    })
    expect(seen).toEqual([true])
    expect(store.getState().isSubmitting).toBe(false)
  })

  test("errors never block a later submit", async () => {
    const store = createDraftStore(draft())
    let runs = 0
    const handler = async () => {
      runs += 1
      store.setServerErrors({ fields: { tag: "rejected" } })
    }
    await store.submit(handler)
    await store.submit(handler)
    expect(runs).toBe(2)
  })

  test("validate stops the submit and shows messages like server errors", async () => {
    const store = createDraftStore(draft())
    store.setValidate((values) => ({ tag: values.tag === "a" ? "taken" : "" }))
    let ran = false
    await store.submit(() => {
      ran = true
    })
    expect(ran).toBe(false)
    expect(store.errorFor("tag")).toBe("taken")
    store.setValue("tag", "b")
    expect(store.errorFor("tag")).toBe(null)
    await store.submit(() => {
      ran = true
    })
    expect(ran).toBe(true)
  })

  test("isSubmitting is reset when the handler throws", async () => {
    const store = createDraftStore(draft())
    await expect(
      store.submit(() => {
        throw new Error("x")
      })
    ).rejects.toThrow("x")
    expect(store.getState().isSubmitting).toBe(false)
  })

  test("a second submit while one runs is ignored", async () => {
    const store = createDraftStore(draft())
    let runs = 0
    const first = store.submit(async () => {
      runs += 1
      await new Promise((resolve) => setTimeout(resolve, 5))
    })
    await store.submit(() => {
      runs += 1
    })
    await first
    expect(runs).toBe(1)
  })
})

describe("api error splitting", () => {
  const resolvePath = (path: string) =>
    path === "outbounds.x.interface"
      ? "interfaceName"
      : path === "outbounds.x.members[0].weight"
        ? "outboundGroups[0].members[0].weight"
        : undefined
  const details = {
    validation_errors: [
      { path: "outbounds.x.interface", message: "required" },
      { path: "outbounds.x.interface", message: "unknown" },
      { path: "outbounds.x.members[0].weight", message: "1..100" },
      { path: "daemon.thing", message: "elsewhere" },
    ],
  }

  test("maps fields, joins duplicates, keeps the rest unmapped", () => {
    const result = splitFormApiErrors({ error: apiError(details), resolvePath })
    expect(result.formError).toBe(null)
    expect(result.fieldErrors).toEqual({
      interfaceName: "required unknown",
      "outboundGroups[0].members[0].weight": "1..100",
    })
    expect(result.unmappedErrors).toEqual([
      { path: "daemon.thing", message: "elsewhere" },
    ])
  })

  test("an error without validation details becomes the form message", () => {
    const result = splitFormApiErrors({
      error: apiError(undefined, "server down"),
      resolvePath,
    })
    expect(result.formError).toBe("server down")
  })

  test("fieldNames restricts what counts as a field", () => {
    const result = splitFormApiErrors({
      error: apiError(details),
      fieldNames: ["interfaceName"],
      resolvePath,
    })
    expect(Object.keys(result.fieldErrors)).toEqual(["interfaceName"])
    expect(result.unmappedErrors).toHaveLength(2)
  })

  test("apiErrorsToServerErrors feeds setServerErrors", () => {
    const store = createDraftStore(draft())
    store.setServerErrors(
      apiErrorsToServerErrors(apiError(details), resolvePath)
    )
    expect(store.errorFor("outboundGroups[0].members[0].weight")).toBe("1..100")
    expect(store.getState().errors.unmapped).toHaveLength(1)
  })
})
