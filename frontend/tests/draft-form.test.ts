import { describe, expect, test } from "bun:test"

import type { ApiError } from "../src/api/client"
import {
  apiErrorsToFormErrors,
  createDraftStore,
  deepEqual,
  formatPath,
  getIn,
  isAtOrBelow,
  joinPath,
  nearestClaimed,
  normalizePath,
  parsePath,
  relativePath,
  routeErrors,
  setIn,
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

  test("quoted keys and canonical spelling", () => {
    expect(parsePath('lists["my.list"].url')).toEqual([
      "lists",
      "my.list",
      "url",
    ])
    expect(normalizePath('lists["my_list"].url')).toBe("lists.my_list.url")
    expect(normalizePath('a["2"].b')).toBe("a[2].b")
    expect(formatPath(["lists", "bad.name"])).toBe('lists["bad.name"]')
    expect(formatPath(["lists", ""])).toBe('lists[""]')
    expect(parsePath('lists[""]')).toEqual(["lists", ""])
    expect(parsePath('a["q\\"x"]')).toEqual(["a", 'q"x'])
    expect(normalizePath(formatPath(["a", 'q"x', "1"]))).toBe('a["q\\"x"][1]')
  })

  test("joinPath and relativePath", () => {
    expect(joinPath("outbounds[3]", "retry.attempts")).toBe(
      "outbounds[3].retry.attempts"
    )
    expect(joinPath("outbounds[3]", "outbound_groups[0]")).toBe(
      "outbounds[3].outbound_groups[0]"
    )
    expect(joinPath("outbounds[3]", "")).toBe("outbounds[3]")
    expect(joinPath("", "a.0")).toBe("a[0]")
    expect(relativePath("outbounds[3]", "outbounds[3].url")).toBe("url")
    expect(relativePath("outbounds[3]", "outbounds[3]")).toBe("")
    expect(relativePath("outbounds[3]", "outbounds[30].url")).toBe(null)
    expect(relativePath("outbounds[3]", "dns.x")).toBe(null)
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

// Simulates a render that shows controls for `paths` (draft paths), then a commit.
const render = <T>(
  store: ReturnType<typeof createDraftStore<T>>,
  paths: string[]
) => {
  store.beginRender()
  paths.forEach((path) => store.claim(path))
  store.commitClaims()
}
const allPaths = [
  "tag",
  "groups",
  "groups[0].members[0].weight",
  "groups[1].members[1].target",
  "groupsExtra",
]

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
    render(store, allPaths)
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

  test("form and extra errors stay until cleared or resubmitted", () => {
    const store = withErrors()
    store.setValue("tag", "z")
    expect(store.getState().errors.form).toBe("boom")
    expect(store.unmapped()).toEqual([{ path: "x", message: "y" }])
    store.clearServerErrors()
    expect(store.getState().errors.form).toBe(null)
    expect(store.errorFor("groupsExtra")).toBe(null)
  })

  test("paths are matched in any spelling", () => {
    const store = createDraftStore(draft())
    store.setServerErrors({ fields: { "groups.0.members.1.target": "bad" } })
    render(store, ["groups[0].members[1].target"])
    expect(store.errorFor("groups[0].members[1].target")).toBe("bad")
  })
})

describe("apiPrefix", () => {
  const apiErrors = {
    validation_errors: [
      { path: "outbounds[3].tag", message: "bad tag" },
      { path: "outbounds[3].groups[0].members[0].weight", message: "weight" },
      { path: "outbounds[4].tag", message: "other outbound" },
    ],
  }
  const prefixed = (prefix: string | ((v: Draft) => string)) =>
    createDraftStore(
      draft(),
      typeof prefix === "function" ? prefix : () => prefix
    )

  test("draft paths resolve under the prefix; foreign paths stay unmapped", () => {
    const store = prefixed("outbounds[3]")
    store.setApiError(apiError(apiErrors))
    render(store, ["tag", "groups[0].members[0].weight"])
    expect(store.errorFor("tag")).toBe("bad tag")
    expect(store.errorFor("groups[0].members[0].weight")).toBe("weight")
    expect(store.unmapped()).toEqual([
      { path: "outbounds[4].tag", message: "other outbound" },
    ])
    expect(store.errorPaths().sort()).toEqual([
      "groups[0].members[0].weight",
      "tag",
    ])
  })

  test("the prefix may depend on the values", () => {
    const store = prefixed((v) => formatPath(["lists", v.tag]))
    store.setApiError(
      apiError({ validation_errors: [{ path: "lists.a.url", message: "u" }] })
    )
    render(store, ["url"])
    expect(store.errorFor("url")).toBe("u")
  })

  test("editing clears the error under the prefix only", () => {
    const store = prefixed("outbounds[3]")
    store.setApiError(apiError(apiErrors))
    render(store, ["tag"])
    store.setValue("tag", "x")
    expect(store.errorFor("tag")).toBe(null)
    expect(store.unmapped().map((entry) => entry.path)).toEqual([
      "outbounds[3].groups[0].members[0].weight",
      "outbounds[4].tag",
    ])
  })

  test("manual and client errors are by draft path", async () => {
    const store = prefixed("dns.rules[2]")
    store.setServerErrors({ fields: { tag: "manual" } })
    render(store, ["tag"])
    expect(store.errorFor("tag")).toBe("manual")
    expect(Object.keys(store.getState().errors.fields)).toEqual([
      "dns.rules[2].tag",
    ])
    store.setValidate(() => ({ tag: "client" }))
    await store.submit(() => {})
    expect(store.errorFor("tag")).toBe("client")
  })
})

describe("claims and ancestor fallback", () => {
  test("nearestClaimed picks the closest claimed ancestor", () => {
    const claims = new Set(["a", "a[0]", "a[0].b.c"])
    expect(nearestClaimed("a[0].b.c", claims)).toBe("a[0].b.c")
    expect(nearestClaimed("a[0].b.c.d", claims)).toBe("a[0].b.c")
    expect(nearestClaimed("a[0].b", claims)).toBe("a[0]")
    expect(nearestClaimed("a[1].b", claims)).toBe("a")
    expect(nearestClaimed("aExtra", claims)).toBe(null)
    expect(nearestClaimed("z", claims)).toBe(null)
    expect(nearestClaimed("z", new Set([""]))).toBe("")
  })

  test("routeErrors groups messages and leaves the unclaimed unmapped", () => {
    const result = routeErrors(
      { "a[0].members": "empty", "a[0].members[1].weight": "w", other: "o" },
      new Set(["a[0]", "a[0].members[1].weight"])
    )
    expect(result.byClaim).toEqual({
      "a[0]": "empty",
      "a[0].members[1].weight": "w",
    })
    expect(result.unmapped).toEqual([{ path: "other", message: "o" }])
  })

  test("an error moves to the specific control once it is rendered", () => {
    const store = createDraftStore(draft())
    store.setServerErrors({ fields: { "groups[0].members[0].weight": "w" } })
    // weight input not shown: the tier container shows the error
    render(store, ["groups[0]"])
    expect(store.errorFor("groups[0]")).toBe("w")
    expect(store.unmapped()).toEqual([])
    // weight input rendered after a re-render: it takes over
    render(store, ["groups[0]", "groups[0].members[0].weight"])
    expect(store.errorFor("groups[0].members[0].weight")).toBe("w")
    expect(store.errorFor("groups[0]")).toBe(null)
  })

  test("an error nobody claims is unmapped with its full path", () => {
    const store = createDraftStore(draft(), () => "x[1]")
    store.setApiError(
      apiError({ validation_errors: [{ path: "x[1].nowhere", message: "m" }] })
    )
    render(store, ["tag"])
    expect(store.unmapped()).toEqual([{ path: "x[1].nowhere", message: "m" }])
  })

  test("alsoClaims lets one control show several paths, '' is the root", () => {
    const store = createDraftStore(draft(), () => "x[1]")
    store.setApiError(
      apiError({
        validation_errors: [
          { path: "x[1]", message: "whole" },
          { path: "x[1].tag", message: "tag" },
          { path: "x[1].groups", message: "groups" },
        ],
      })
    )
    store.beginRender()
    store.claim("tag", { alsoClaims: ["groups", ""] })
    store.commitClaims()
    expect(store.errorFor("tag", { alsoClaims: ["groups", ""] })).toBe(
      "tag groups whole"
    )
    expect(store.unmapped()).toEqual([])
  })

  test("commitClaims re-notifies only when errors exist and claims changed", () => {
    const store = createDraftStore(draft())
    let calls = 0
    store.subscribe(() => calls++)
    render(store, ["tag"])
    expect(calls).toBe(0)
    store.setServerErrors({ fields: { tag: "e" } })
    calls = 0
    render(store, ["tag"])
    expect(calls).toBe(0)
    render(store, ["tag", "groups"])
    expect(calls).toBe(1)
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
    render(store, ["tag"])
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

describe("api errors", () => {
  test("keeps exact normalized paths and joins messages on the same path", () => {
    const result = apiErrorsToFormErrors(
      apiError({
        validation_errors: [
          { path: "outbounds.0.url", message: "a" },
          { path: "outbounds[0].url", message: "b" },
          { path: 'lists["x.y"]', message: "c" },
        ],
      })
    )
    expect(result.form).toBe(null)
    expect(result.fields).toEqual({
      "outbounds[0].url": "a b",
      'lists["x.y"]': "c",
    })
  })

  test("an error without validation details becomes the form message", () => {
    const store = createDraftStore(draft())
    expect(store.setApiError(apiError(undefined, "server down"))).toBe(
      "server down"
    )
    expect(store.getState().errors.form).toBe("server down")
  })

  test("a missing error yields no errors", () => {
    expect(apiErrorsToFormErrors(null).fields).toEqual({})
  })
})

test("clearing an empty hidden source removes its error before it can fall back to name", () => {
  const store = createDraftStore({
    name: "asda",
    source: "url",
    url: "",
    file: "",
  })
  store.setServerErrors({ fields: { url: "Поле обязательно" } })
  store.beginRender()
  store.errorFor("name", { alsoClaims: [""] })
  store.errorFor("url")
  store.commitClaims()
  store.setValue("source", "file")
  store.setValue("url", "")
  store.beginRender()
  store.errorFor("name", { alsoClaims: [""] })
  store.errorFor("file")
  store.commitClaims()
  expect(store.errorFor("name", { alsoClaims: [""] })).toBeNull()
  expect(store.errorFor("file")).toBeNull()
  store.setServerErrors({ fields: { file: "Поле обязательно" } })
  store.setValue("source", "url")
  store.setValue("file", "")
  store.beginRender()
  store.errorFor("name", { alsoClaims: [""] })
  store.errorFor("url")
  store.commitClaims()
  expect(store.errorFor("name", { alsoClaims: [""] })).toBeNull()
  expect(store.errorFor("url")).toBeNull()
})
