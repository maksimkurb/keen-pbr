import type { DnsCheckRequest } from "@/api/generated/model/dnsCheckRequest"
import type { DnsCheckResponse } from "@/api/generated/model/dnsCheckResponse"
import type { DnsCheckServer } from "@/api/generated/model/dnsCheckServer"

/** Sites offered in the menu of the check button, in this order. */
export const PRESET_DOMAINS = [
  "yandex.ru",
  "telegram.org",
  "youtube.com",
  "google.com",
  "instagram.com",
  "chatgpt.com",
] as const

export const DEFAULT_PRESETS: readonly string[] = ["yandex.ru", "telegram.org"]
/** The daemon refuses more domains than this in one check. */
export const MAX_CHECK_DOMAINS = 5
export const CHECK_STORAGE_KEY = "keen-pbr-dns-check-domains"

export type CheckSelection = {
  presets: string[]
  custom: string
}

type ReadStorage = Pick<Storage, "getItem">
type WriteStorage = Pick<Storage, "setItem">

export function defaultSelection(): CheckSelection {
  return { presets: [...DEFAULT_PRESETS], custom: "" }
}

function browserStorage(): Storage | undefined {
  try {
    return globalThis.localStorage
  } catch {
    return undefined
  }
}

/** The remembered choice; the default one when nothing usable is stored. */
export function loadSelection(storage?: ReadStorage): CheckSelection {
  try {
    const source = storage ?? browserStorage()
    const raw = source?.getItem(CHECK_STORAGE_KEY)
    if (!raw) return defaultSelection()
    const parsed: unknown = JSON.parse(raw)
    if (typeof parsed !== "object" || parsed === null) return defaultSelection()
    const { presets, custom } = parsed as Record<string, unknown>
    if (!Array.isArray(presets)) return defaultSelection()
    return {
      presets: PRESET_DOMAINS.filter((domain) => presets.includes(domain)),
      custom: typeof custom === "string" ? custom : "",
    }
  } catch {
    return defaultSelection()
  }
}

/** Remembers the choice; works without storage (private windows, blocked). */
export function saveSelection(
  selection: CheckSelection,
  storage?: WriteStorage
): void {
  try {
    const target = storage ?? browserStorage()
    target?.setItem(CHECK_STORAGE_KEY, JSON.stringify(selection))
  } catch {
    // The choice is just not remembered.
  }
}

const LABEL = /^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$/i

export function isValidDomain(value: string): boolean {
  if (value.length === 0 || value.length > 253) return false
  return value.split(".").every((label) => LABEL.test(label))
}

export type SelectionProblem = "empty" | "tooMany" | "invalidCustom" | null

export function selectionProblem(selection: CheckSelection): SelectionProblem {
  const custom = selection.custom.trim().toLowerCase()
  if (custom !== "" && !isValidDomain(custom)) return "invalidCustom"
  const total = selection.presets.length + (custom !== "" ? 1 : 0)
  if (total === 0) return "empty"
  if (total > MAX_CHECK_DOMAINS) return "tooMany"
  return null
}

export function buildCheckRequest(selection: CheckSelection): DnsCheckRequest {
  const custom = selection.custom.trim().toLowerCase()
  const domains = PRESET_DOMAINS.filter((domain) =>
    selection.presets.includes(domain)
  )
  const request: DnsCheckRequest = { domains }
  if (custom !== "" && !domains.includes(custom as never)) {
    request.custom_domains = [custom]
  }
  return request
}

export type CheckLevel = "ok" | "warning" | "error"

function level(value: string): CheckLevel {
  return value === "error" || value === "warning" ? value : "ok"
}

const RANK: Record<CheckLevel, number> = { ok: 0, warning: 1, error: 2 }

function worse(a: CheckLevel, b: CheckLevel): CheckLevel {
  return RANK[b] > RANK[a] ? b : a
}

/** How one server came out: its worst endpoint. `skipped` when it was not asked. */
export function serverLevel(server: DnsCheckServer): CheckLevel | "skipped" {
  if (server.error) return "error"
  if (server.endpoints.length === 0) return "skipped"
  let result: CheckLevel = "ok"
  let asked = false
  for (const endpoint of server.endpoints) {
    if (endpoint.status === "skipped") continue
    asked = true
    if (endpoint.status === "unavailable") {
      result = worse(result, "error")
      continue
    }
    if (endpoint.nxdomain_hijack) result = worse(result, "warning")
    for (const item of endpoint.results) {
      result = worse(result, level(item.severity))
    }
  }
  return asked ? result : "skipped"
}

export type CheckProblem = {
  tag: string
  address: string
  /** The status that tells what is wrong: `timeout`, `nxdomain`, `hijack`, ... */
  status: string
  domain?: string
  detail?: string
}

export type CheckSummary = {
  level: CheckLevel
  /** Servers that do not answer or cannot be asked. */
  errors: CheckProblem[]
  /** Servers that answer, but look tampered with. */
  warnings: CheckProblem[]
}

export const HIJACK_STATUS = "nxdomain_hijack"

/** The text of the result alert is built from this. */
export function summarizeCheck(response: DnsCheckResponse): CheckSummary {
  const errors: CheckProblem[] = []
  const warnings: CheckProblem[] = []
  for (const server of response.servers) {
    if (server.error) {
      errors.push({
        tag: server.tag,
        address: "",
        status: server.error_code ?? "error",
        detail: server.error,
      })
    }
    for (const endpoint of server.endpoints) {
      const base = { tag: server.tag, address: endpoint.address }
      if (endpoint.status === "unavailable") {
        errors.push({
          ...base,
          status: endpoint.reason_code ?? "unavailable",
          detail: endpoint.reason,
        })
        continue
      }
      // A server that does not answer at all is told once, not per domain.
      const noAnswer = endpoint.results.filter(
        (item) => item.severity === "error" && item.rcode === undefined
      )
      if (noAnswer.length > 0 && noAnswer.length === endpoint.results.length) {
        errors.push({
          ...base,
          status: noAnswer[0].status,
          detail: noAnswer[0].error,
        })
        continue
      }
      for (const item of endpoint.results) {
        const problem = {
          ...base,
          status: item.status,
          domain: item.domain,
          detail: item.error,
        }
        if (item.severity === "error") errors.push(problem)
        else if (item.severity === "warning") warnings.push(problem)
      }
      if (endpoint.nxdomain_hijack) {
        warnings.push({ ...base, status: HIJACK_STATUS })
      }
    }
  }
  return {
    level: errors.length > 0 ? "error" : warnings.length > 0 ? "warning" : "ok",
    errors,
    warnings,
  }
}
