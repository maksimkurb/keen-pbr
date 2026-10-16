import {
  buildOutboundPayload,
  mapOutboundToDraft,
} from "../src/pages/outbound-upsert-utils"
import { describe, expect, test } from "bun:test"
import { Router } from "wouter"
import { createInstance } from "i18next"
import { isValidElement, type ReactNode, type ReactElement } from "react"
import { I18nextProvider } from "react-i18next"
import { renderToStaticMarkup } from "react-dom/server"
import { QueryClient, QueryClientProvider } from "@tanstack/react-query"

import { RoutingRuleConditionBuilder } from "../src/components/routing-rules/routing-rule-condition-builder"
import { RoutingRuleConditionPicker } from "../src/components/routing-rules/routing-rule-condition-picker"
import { OutboundUpsertPage } from "../src/pages/outbound-upsert-page"
import { ListUpsertPage } from "../src/pages/list-upsert-page"
import { RoutingRuleUpsertPage } from "../src/pages/routing-rule-upsert-page"
import { Field, FieldHint, FieldLabel } from "../src/components/shared/field"
import { Input } from "../src/components/ui/input"
import {
  getGetConfigQueryKey,
  getGetRuntimeOutboundsQueryKey,
} from "../src/api/generated/keen-api"
import type { RouteRule } from "../src/api/generated/model/routeRule"
import { createDraftStore } from "../src/lib/draft-form-core"
import {
  emptyRouteRuleDraft,
  getActiveRouteConditions,
  normalizeRouteRuleDraft,
  routeConditionKeys,
  toRouteRuleDraft,
  validateDscp,
  type RouteConditionKey,
} from "../src/pages/routing-rules-utils"
import { enTranslation } from "../src/i18n/en"
import { ruTranslation } from "../src/i18n/ru"

const i18n = createInstance()
await i18n.init({
  lng: "ru",
  resources: {
    ru: { translation: ruTranslation },
    en: { translation: enTranslation },
  },
  fallbackLng: false,
})
const t = (key: string, options?: Record<string, unknown>) =>
  i18n.t(key, options)

function renderBuilder(active: RouteConditionKey[], isNormalRule = true) {
  return renderToStaticMarkup(
    <I18nextProvider i18n={i18n}>
      <RoutingRuleConditionBuilder
        activeConditions={active}
        controls={Object.fromEntries(
          active.map((key) => [key, <input key={key} aria-label={key} />])
        )}
        isNormalRule={isNormalRule}
        onAdd={() => {}}
        onRemove={() => {}}
        outbound={
          <select defaultValue="vpn">
            <option value="vpn">vpn</option>
          </select>
        }
      />
    </I18nextProvider>
  )
}

// Invoke the actual picker button/menu-item handlers without adding a DOM dependency.
function findOption(
  node: ReactNode,
  key: string
): ReactElement<{ children?: ReactNode; onClick?: () => void }> | undefined {
  if (Array.isArray(node)) {
    for (const child of node) {
      const found = findOption(child, key)
      if (found) return found
    }
    return undefined
  }
  if (!isValidElement<{ children?: ReactNode; onClick?: () => void }>(node))
    return undefined
  return node.key === key ? node : findOption(node.props.children, key)
}

function session(rule?: RouteRule) {
  const store = createDraftStore(
    rule ? toRouteRuleDraft(rule) : { ...emptyRouteRuleDraft, list: [] }
  )
  let opened = getActiveRouteConditions(store.getState().values, [])
  const active = () =>
    getActiveRouteConditions(
      store.getState().values,
      opened,
      store.errorPaths()
    )
  const picker = () =>
    RoutingRuleConditionPicker({
      mode: active().length ? "additional" : "initial",
      available: routeConditionKeys.filter((key) => !active().includes(key)),
      t,
      onAdd: (key) => {
        if (!opened.includes(key)) opened = [...active(), key]
      },
    })
  const click = (key: RouteConditionKey) => {
    const option = findOption(picker(), key)
    expect(option).toBeDefined()
    option!.props.onClick!()
  }
  const remove = (key: RouteConditionKey) => {
    store.setValue(key, key === "list" ? [] : "")
    opened = opened.filter((item) => item !== key)
  }
  return { store, active, picker, click, remove }
}

describe("routing condition builder", () => {
  test("empty create shows only the first-condition tiles", () => {
    const html = renderBuilder(session().active())
    expect(html).toContain('data-slot="initial-condition-picker"')
    expect(html).toContain("Выберите первое условие")
    expect(html.match(/type="button"/g)).toHaveLength(7)
    expect(html).not.toContain('data-slot="condition-flow"')
    expect(html).not.toContain('data-slot="add-condition-picker"')
    expect(html).not.toContain('data-slot="routing-outbound-action"')
  })

  test("first click replaces tiles; remaining menu excludes used conditions; removal restores them", () => {
    const form = session()
    form.click("list")
    expect(form.store.getState().values.list).toEqual([])
    expect(form.active()).toEqual(["list"])
    let html = renderBuilder(form.active())
    expect(html).not.toContain('data-slot="initial-condition-picker"')
    expect(html).toContain('data-condition="list"')
    expect(html).toContain('data-slot="add-condition-picker"')
    expect(html).toContain('data-slot="routing-outbound-action"')
    expect(findOption(form.picker(), "list")).toBeUndefined()
    for (const key of routeConditionKeys.slice(1))
      expect(findOption(form.picker(), key)).toBeDefined()
    form.store.setValue("list", ["work", "home"])
    form.store.setValue("outbound", "vpn")
    form.click("proto")
    form.store.setValue("proto", "udp")
    html = renderBuilder(form.active())
    expect(form.active()).toEqual(["list", "proto"])
    expect(html).toContain(">ЕСЛИ</span>")
    expect(html).toContain(">И</span>")
    expect(html).not.toMatch(/>[1-7]<\/span>/)
    expect(html).toContain(">тогда</span>")
    expect(html).toContain('aria-label="Удалить условие «Протокол»"')
    expect(findOption(form.picker(), "list")).toBeUndefined()
    expect(findOption(form.picker(), "proto")).toBeUndefined()
    form.remove("proto")
    expect(form.store.getState().values.proto).toBe("")
    expect(form.store.getState().values.list).toEqual(["work", "home"])
    expect(findOption(form.picker(), "proto")).toBeDefined()
    form.remove("list")
    expect(form.active()).toEqual([])
    expect(form.store.getState().values.outbound).toBe("vpn")
    expect(renderBuilder(form.active())).toContain(
      'data-slot="initial-condition-picker"'
    )
    expect(renderBuilder(form.active())).not.toContain(
      'data-slot="condition-flow"'
    )
  })

  test("existing rules reconstruct in canonical order and preserve payload syntax", () => {
    const rule: RouteRule = {
      enabled: false,
      list: ["work", "home"],
      proto: "udp",
      dscp: 46,
      src_port: "!80,443",
      dest_port: "1000-2000",
      src_addr: "192.168.1.10,10.0.0.0/8",
      dest_addr: "!2001:db8::/32",
      outbound: "ignore",
    }
    const form = session(rule)
    expect(form.active()).toEqual([...routeConditionKeys])
    expect(renderBuilder(form.active())).not.toContain(
      'data-slot="initial-condition-picker"'
    )
    expect(renderBuilder(form.active())).toContain(
      "Все доступные условия уже добавлены"
    )
    expect(normalizeRouteRuleDraft(form.store.getState().values)).toEqual(rule)
    expect(findOption(form.picker(), "list")).toBeUndefined()
  })

  test("blank editors never inject predicates or change outbound; removing clears the API value", () => {
    const form = session({ list: ["work"], outbound: "vpn" })
    form.click("dest_addr")
    expect(normalizeRouteRuleDraft(form.store.getState().values)).toEqual({
      enabled: true,
      list: ["work"],
      outbound: "vpn",
      proto: undefined,
      dscp: undefined,
      src_port: undefined,
      dest_port: undefined,
      src_addr: undefined,
      dest_addr: undefined,
    })
    form.store.setValue("dest_addr", "  !2001:db8::/32  ")
    expect(
      normalizeRouteRuleDraft(form.store.getState().values).dest_addr
    ).toBe("!2001:db8::/32")
    form.remove("dest_addr")
    expect(
      normalizeRouteRuleDraft(form.store.getState().values).dest_addr
    ).toBeUndefined()
  })

  test("gateway modes hide conditions and serialize only the existing gateway fields", () => {
    for (const gateway of ["ipv4", "ipv6"] as const) {
      const form = session({ list: ["work"], proto: "udp", outbound: "vpn" })
      form.store.setValue("default_gateway", gateway)
      expect(form.active()).toEqual([])
      const html = renderBuilder(form.active(), false)
      expect(html).not.toContain("condition-picker")
      expect(html).not.toContain("data-condition=")
      expect(html).not.toContain(">тогда</span>")
      expect(html).not.toContain("grid-cols-[2.25rem_minmax(0,1fr)]")
      expect(html).toContain('data-slot="routing-outbound-action"')
      expect(normalizeRouteRuleDraft(form.store.getState().values)).toEqual({
        enabled: true,
        default_gateway: gateway,
        outbound: "vpn",
      })
    }
  })

  test("client DSCP bounds and server port/address errors remain attached to conditions", () => {
    for (const value of ["0", "64", "99", "1.5", "abc"])
      expect(validateDscp(value, t)).toBe(
        t("pages.routingRuleUpsert.validation.dscpRange")
      )
    for (const value of ["", "  ", "1", "63"])
      expect(validateDscp(value, t)).toBeUndefined()
    const store = createDraftStore(
      { ...emptyRouteRuleDraft },
      () => "route.rules[2]"
    )
    store.beginRender()
    for (const key of ["dest_port", "src_addr", "dscp"] as const)
      store.errorFor(key)
    store.commitClaims()
    store.setServerErrors({
      fields: {
        dscp: "DSCP error",
        dest_port: "Port error",
        src_addr: "Address error",
      },
    })
    const active = getActiveRouteConditions(
      store.getState().values,
      [],
      store.errorPaths()
    )
    expect(active).toEqual(["dest_port", "src_addr", "dscp"])
    const controls = Object.fromEntries(
      active.map((key) => [
        key,
        <Field>
          <FieldLabel htmlFor={key}>{key}</FieldLabel>
          <Input id={key} aria-invalid />
          <FieldHint error={store.errorFor(key)} />
        </Field>,
      ])
    )
    const html = renderToStaticMarkup(
      <I18nextProvider i18n={i18n}>
        <RoutingRuleConditionBuilder
          activeConditions={active}
          controls={controls}
          isNormalRule
          onAdd={() => {}}
          onRemove={() => {}}
          outbound="vpn"
        />
      </I18nextProvider>
    )
    for (const [key, error] of [
      ["dscp", "DSCP error"],
      ["dest_port", "Port error"],
      ["src_addr", "Address error"],
    ])
      expect(html).toMatch(
        new RegExp(`data-condition="${key}"[\\s\\S]*?${error}`)
      )
    expect(store.unmapped()).toEqual([])
  })

  test("mobile layout uses shrinkable columns and wrapping actions, not fixed-width rows", () => {
    const html = renderBuilder(["list", "proto"])
    expect(html).toContain("grid-cols-[2.25rem_minmax(0,1fr)]")
    expect(html).toContain("min-w-0")
    expect(html).toContain("flex-wrap")
    expect(html).toContain("size-11")
    expect(html).not.toContain("min-h-11")
    expect(html).not.toContain("min-w-80")
  })
})

function renderPage(rule?: RouteRule, page?: ReactNode) {
  const client = new QueryClient({
    defaultOptions: { queries: { retry: false } },
  })
  client.setQueryData(getGetConfigQueryKey(), {
    status: 200,
    data: {
      config: {
        lists: { work: {} },
        outbounds: [{ type: "ignore", tag: "vpn" }],
        route: { rules: rule ? [rule] : [] },
      },
      is_draft: false,
    },
  })
  client.setQueryData(getGetRuntimeOutboundsQueryKey(), {
    status: 200,
    data: { outbounds: [] },
  })
  try {
    return renderToStaticMarkup(
      <QueryClientProvider client={client}>
        <I18nextProvider i18n={i18n}>
          <Router ssrPath="/routing-rules/new">
            {page ?? (
              <RoutingRuleUpsertPage
                mode={rule ? "edit" : "create"}
                ruleIndex={rule ? "0" : undefined}
              />
            )}
          </Router>
        </I18nextProvider>
      </QueryClientProvider>
    )
  } finally {
    client.clear()
  }
}

test("real create/edit pages use the same builder and existing outbound selector", () => {
  expect(renderPage()).toContain('data-slot="initial-condition-picker"')
  expect(renderPage()).not.toContain('data-slot="card"')
  expect(renderPage()).not.toContain('data-slot="card-header"')
  expect(renderPage()).toContain("Включить правило")
  expect(renderPage().indexOf('id="routing-rule-enabled"')).toBeLessThan(
    renderPage().indexOf('id="routing-rule-mode"')
  )
  const html = renderPage({
    list: ["work"],
    proto: "udp",
    src_addr: "192.168.1.10",
    outbound: "vpn",
  })
  expect(html).not.toContain('data-slot="initial-condition-picker"')
  expect(html).not.toContain('data-slot="card"')
  expect(html).toContain(
    'Изменить правило маршрутизации <span class="text-primary">#1</span>'
  )
  for (const key of ["list", "proto", "src_addr"])
    expect(html).toContain(`data-condition="${key}"`)
  expect(html).toContain('value="192.168.1.10"')
  expect(html).toContain('aria-labelledby="routing-outbound-label"')
  expect(html).toContain("vpn")
  const gateway = renderPage({ default_gateway: "ipv4", outbound: "vpn" })
  expect(gateway).not.toContain("data-condition=")
  expect(gateway).toContain('data-slot="routing-outbound-action"')
})

test("rule type displays localized labels instead of its raw value", () => {
  for (const [rule, label] of [
    [undefined, "Условная маршрутизация"],
    [{ default_gateway: "ipv4", outbound: "vpn" }, "Шлюз по умолчанию IPv4"],
    [{ default_gateway: "ipv6", outbound: "vpn" }, "Шлюз по умолчанию IPv6"],
  ] as const) {
    const html = renderPage(rule)
    const trigger = html.match(
      /<button[^>]*id="routing-rule-mode"[^>]*>[\s\S]*?<\/button>/
    )?.[0]
    expect(trigger).toBeDefined()
    expect(trigger).toContain(label)
  }
})

test("outbound name is disabled only when editing and both names omit hints", () => {
  const edit = renderPage(
    undefined,
    <OutboundUpsertPage mode="edit" outboundId="vpn" />
  )
  const tag = edit.match(/<input[^>]*name="tag"[^>]*>/)?.[0]
  expect(tag).toBeDefined()
  expect(tag).toContain('disabled=""')
  expect(tag).not.toContain("readonly")
  expect(edit).toContain(
    'Изменить outbound <span class="text-primary">vpn</span>'
  )
  const create = renderPage(undefined, <OutboundUpsertPage mode="create" />)
  expect(create.match(/<input[^>]*name="tag"[^>]*>/)?.[0]).not.toContain(
    'disabled=""'
  )
  expect(ruTranslation.pages.outboundUpsert.fields).not.toHaveProperty(
    "tagHint"
  )
  expect(enTranslation.pages.outboundUpsert.fields).not.toHaveProperty(
    "tagHint"
  )
  const list = renderPage(
    undefined,
    <ListUpsertPage mode="edit" listId="work" />
  )
  expect(list).toContain(
    'Изменить список <span class="text-primary">work</span>'
  )
  expect(list.match(/<input[^>]*id="list-name"[^>]*>/)?.[0]).toContain(
    "max-w-sm"
  )
  expect(list.match(/<input[^>]*id="list-ttl-ms"[^>]*>/)?.[0]).toContain(
    "max-w-sm"
  )
  expect(ruTranslation.pages.listUpsert.fields.name).toBe("Название")
  expect(ruTranslation.pages.listUpsert.fields).not.toHaveProperty("nameHint")
  expect(enTranslation.pages.listUpsert.fields).not.toHaveProperty("nameHint")
})

test("new conditions append for the session, re-added conditions move to the end, reopening restores canonical order", () => {
  const form = session({
    list: ["work"],
    dest_addr: "10.0.0.0/8",
    outbound: "vpn",
  })
  form.click("proto")
  form.store.setValue("proto", "udp")
  form.click("dscp")
  form.store.setValue("dscp", "46")
  expect(form.active()).toEqual(["list", "dest_addr", "proto", "dscp"])
  form.remove("list")
  form.click("list")
  form.store.setValue("list", ["home"])
  expect(form.active()).toEqual(["dest_addr", "proto", "dscp", "list"])
  const saved = normalizeRouteRuleDraft(form.store.getState().values)
  expect(saved).not.toHaveProperty("condition_order")
  expect(session(saved).active()).toEqual([
    "list",
    "proto",
    "dest_addr",
    "dscp",
  ])

  const create = session()
  create.click("dest_addr")
  create.click("list")
  create.click("proto")
  expect(create.active()).toEqual(["dest_addr", "list", "proto"])
})

test.skipIf(import.meta.env.VITE_KEEN_PBR_PLATFORM !== "keenetic")(
  "Keenetic builds hide the rule type selector while keeping conditional routing",
  () => {
    const html = renderPage()
    expect(html).not.toContain('id="routing-rule-mode"')
    expect(html).not.toContain("Тип правила")
    expect(html).toContain('data-slot="initial-condition-picker"')
    expect(html).toContain("Включить правило")
  }
)

test("pristine create forms allow validation; pristine edit forms disable save", () => {
  const submitButton = (html: string) =>
    html.match(/<button\b[^>]*type="submit"[^>]*>/)?.[0]
  for (const page of [
    <RoutingRuleUpsertPage mode="create" />,
    <ListUpsertPage mode="create" />,
    <OutboundUpsertPage mode="create" />,
  ]) {
    const button = submitButton(renderPage(undefined, page))
    expect(button).toBeDefined()
    expect(button).not.toMatch(/\sdisabled(?:=|\s|>)/)
  }
  expect(submitButton(renderPage({ list: ["work"], outbound: "vpn" }))).toMatch(
    /\sdisabled(?:=|\s|>)/
  )
  expect(
    submitButton(
      renderPage(undefined, <ListUpsertPage mode="edit" listId="work" />)
    )
  ).toMatch(/\sdisabled(?:=|\s|>)/)
  expect(
    submitButton(
      renderPage(undefined, <OutboundUpsertPage mode="edit" outboundId="vpn" />)
    )
  ).toMatch(/\sdisabled(?:=|\s|>)/)
})

test("new lists and outbounds require an explicit type selection", () => {
  for (const page of [
    <ListUpsertPage mode="create" />,
    <OutboundUpsertPage mode="create" />,
  ]) {
    const html = renderPage(undefined, page)
    expect(html).not.toContain('aria-checked="true"')
    expect(html).toContain('role="radio"')
    expect(html).not.toContain('id="list-url"')
    expect(html).not.toContain('id="list-file"')
    expect(html).not.toContain('id="list-domains"')
  }
  expect(() =>
    buildOutboundPayload({
      ...mapOutboundToDraft({ tag: "new", type: "ignore" }),
      type: "",
    })
  ).toThrow("Outbound type is required")
})
