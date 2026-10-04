import { describe, expect, test } from "bun:test"
import i18next from "i18next"

import { enTranslation } from "../src/i18n/en"
import { ruTranslation } from "../src/i18n/ru"
import {
  buildRequestFlags,
  formatLocalTime,
  methodBadge,
  type RequestFlagRow,
} from "../src/lib/request-flags"

const en = i18next.createInstance()
await en.init({ lng: "en", resources: { en: { translation: enTranslation } } })
const ru = i18next.createInstance()
await ru.init({ lng: "ru", resources: { ru: { translation: ruTranslation } } })

const ts = new Date(2026, 9, 5, 18, 53, 7, 380).getTime()
const row: RequestFlagRow = {
  added: 2,
  refreshed: 2,
  errors: 1,
  seq: 240,
  ts_ms: ts,
  timed_out: true,
  timeout_cause: "own_write_slow",
  parse_us: 6,
  set_write_us: 59717,
}

describe("buildRequestFlags", () => {
  test("keys and labels for a representative row", () => {
    const flags = buildRequestFlags(row, en.t.bind(en), "en")
    expect(flags.map((f) => f.key)).toEqual([
      "added",
      "refreshed",
      "errors",
      "timeout",
      "parse",
      "set",
      "seq",
      "time",
    ])
    expect(flags.map((f) => f.label)).toEqual([
      "+2",
      "↻2",
      "!1",
      "late: write",
      "parse:6µs",
      "set:59717µs",
      "#240",
      "@18:53:07.380",
    ])
    expect(flags.find((f) => f.key === "errors")?.tone).toBe("danger")
    expect(flags.find((f) => f.key === "timeout")?.tone).toBe("danger")
    expect(flags.find((f) => f.key === "added")?.tone).toBe("neutral")
  })

  test("english tooltips contain the values", () => {
    const tips = Object.fromEntries(
      buildRequestFlags(row, en.t.bind(en), "en").map((f) => [f.key, f.tooltip])
    )
    expect(tips.added).toBe("2 new addresses added to the routing set")
    expect(tips.refreshed).toContain("2 addresses were already in the set")
    expect(tips.errors).toBe("1 address failed to be written to the set")
    expect(tips.seq).toBe("Request #240 (event sequence number)")
    expect(tips.parse).toBe("Response parsed in 6 µs")
    expect(tips.set).toBe("Set write took 59 ms")
    expect(tips.time).toContain("Seen at 18:53:07.380")
    expect(tips.timeout).toBe(
      enTranslation.requestsLog.timeout.own_write_slow.tooltip
    )
  })

  test("russian tooltips use plural forms and localized units", () => {
    const tips = Object.fromEntries(
      buildRequestFlags(row, ru.t.bind(ru), "ru").map((f) => [f.key, f.tooltip])
    )
    expect(tips.added).toBe("Добавлено 2 новых адреса в набор маршрутизации")
    expect(tips.refreshed).toBe("2 адреса уже были в наборе, их срок продлён")
    expect(tips.errors).toBe("1 адрес не удалось записать в набор")
    expect(tips.seq).toBe("Запрос №240 (порядковый номер события)")
    expect(tips.parse).toBe("Ответ разобран за 6 µs")
    expect(tips.set).toBe("Запись в набор заняла 59 мс")
    expect(tips.time).toContain("Замечен в 18:53:07.380")
  })

  test("decimal separator is localized below 10 ms", () => {
    const flags = buildRequestFlags(
      { ...row, set_write_us: 5970 },
      ru.t.bind(ru)
    )
    expect(flags.find((f) => f.key === "set")?.tooltip).toBe(
      "Запись в набор заняла 5,9 мс"
    )
  })

  test("omits flags without data", () => {
    const flags = buildRequestFlags(
      { added: 0, refreshed: 0, errors: 0, seq: 0, ts_ms: 0, timed_out: false },
      en.t.bind(en)
    )
    expect(flags).toEqual([])
  })

  test("local time formatting", () => {
    expect(formatLocalTime(ts)).toBe("18:53:07.380")
  })
})

describe("methodBadge", () => {
  test("labels, tooltips and tones", () => {
    const t = en.t.bind(en)
    expect(methodBadge("dns", t).tooltip).toBe("DNS response")
    expect(methodBadge("http", t).className).toContain("bg-sky-100")
    expect(methodBadge("sni", t).className).toContain("dark:bg-emerald-950")
    expect(methodBadge("quic", t).className).toContain("violet")
    expect(methodBadge("marker", t).variant).toBe("outline")
    expect(methodBadge("sni", t).tooltip).toBe("HTTPS connection (TLS SNI)")
  })
})
