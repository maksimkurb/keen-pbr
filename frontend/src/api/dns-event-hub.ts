import {
  consumeAuthenticatedSse,
  SseHttpError,
  type SseMessage,
} from "@/api/authenticated-sse"
import {
  DnsTestInterceptEventSource,
  type DnsTestInterceptEvent,
} from "@/api/generated/model"

export type DnsEventHubStatus = "idle" | "connecting" | "connected" | "error"

export type DnsEventFailure = {
  kind: "stalled" | "http" | "network"
  status?: number
}

export type ConsumeSse = (
  url: string,
  signal: AbortSignal,
  onMessage: (message: SseMessage) => void,
  onOpen?: () => void
) => Promise<void>

export type HubTimers = {
  setTimeout: (fn: () => void, ms: number) => unknown
  clearTimeout: (handle: unknown) => void
}

export type DnsEventHubOptions = {
  consume?: ConsumeSse
  timers?: HubTimers
  url?: string
}

export const DNS_EVENT_STALL_MS = 5_000
export const DNS_EVENT_IDLE_DISCONNECT_MS = 2_000
export const DNS_EVENT_RECONNECT_MS = 3_000
export const DNS_EVENT_HIDDEN_DISCONNECT_MS = 60_000

export type DnsEventHub = ReturnType<typeof createDnsEventHub>

/**
 * Browsers allow only ~6 HTTP/1.1 connections per host across all tabs, so
 * every tab must hold at most one /api/dns/test stream. The hub owns that
 * stream and fans events out to ref-counted subscribers and check leases.
 */
export function createDnsEventHub(options: DnsEventHubOptions = {}) {
  const consume = options.consume ?? consumeAuthenticatedSse
  const timers: HubTimers = options.timers ?? {
    setTimeout: (fn, ms) => globalThis.setTimeout(fn, ms),
    clearTimeout: (handle) =>
      globalThis.clearTimeout(handle as ReturnType<typeof setTimeout>),
  }
  const url = options.url ?? "/api/dns/test"

  const listeners = new Set<(event: DnsTestInterceptEvent) => void>()
  const statusListeners = new Set<() => void>()
  let leases = 0
  let status: DnsEventHubStatus = "idle"
  let failureState: DnsEventFailure | null = null
  let controller: AbortController | null = null
  let hidden = false
  let paused = false
  let stallTimer: unknown = null
  let reconnectTimer: unknown = null
  let idleTimer: unknown = null
  let hiddenTimer: unknown = null

  const refs = () => listeners.size + leases

  const clear = (handle: unknown) => {
    if (handle !== null) timers.clearTimeout(handle)
    return null
  }

  const publish = (
    nextStatus: DnsEventHubStatus,
    nextFailure: DnsEventFailure | null
  ) => {
    const changed =
      nextStatus !== status ||
      nextFailure?.kind !== failureState?.kind ||
      nextFailure?.status !== failureState?.status
    status = nextStatus
    failureState = nextFailure
    if (changed) for (const cb of [...statusListeners]) cb()
  }

  const settle = (current: AbortController, error: unknown) => {
    if (controller !== current || current.signal.aborted) return
    controller = null
    stallTimer = clear(stallTimer)
    publish(
      "error",
      error instanceof SseHttpError
        ? { kind: "http", status: error.status }
        : { kind: "network" }
    )
    scheduleReconnect()
  }

  const scheduleReconnect = () => {
    reconnectTimer = clear(reconnectTimer)
    if (refs() === 0 || paused) return
    reconnectTimer = timers.setTimeout(() => {
      reconnectTimer = null
      connect()
    }, DNS_EVENT_RECONNECT_MS)
  }

  const connect = () => {
    if (controller !== null || refs() === 0 || paused) return
    reconnectTimer = clear(reconnectTimer)
    const current = new AbortController()
    controller = current
    publish("connecting", failureState)
    stallTimer = clear(stallTimer)
    stallTimer = timers.setTimeout(() => {
      stallTimer = null
      if (controller === current && status === "connecting") {
        publish("connecting", { kind: "stalled" })
      }
    }, DNS_EVENT_STALL_MS)

    void consume(
      url,
      current.signal,
      ({ data }) => {
        if (controller !== current) return
        const payload = parseDnsCheckEvent(data)
        if (payload?.type !== "INTERCEPT") return
        for (const listener of [...listeners]) listener(payload)
      },
      () => {
        if (controller !== current || current.signal.aborted) return
        stallTimer = clear(stallTimer)
        publish("connected", null)
      }
    ).then(
      () => settle(current, null),
      (error) => settle(current, error)
    )
  }

  const disconnect = () => {
    const current = controller
    controller = null
    stallTimer = clear(stallTimer)
    reconnectTimer = clear(reconnectTimer)
    current?.abort()
    publish("idle", null)
  }

  const refAdded = () => {
    idleTimer = clear(idleTimer)
    connect()
  }

  const refRemoved = () => {
    if (refs() > 0) return
    idleTimer = clear(idleTimer)
    idleTimer = timers.setTimeout(() => {
      idleTimer = null
      if (refs() === 0) disconnect()
    }, DNS_EVENT_IDLE_DISCONNECT_MS)
  }

  return {
    subscribe(listener: (event: DnsTestInterceptEvent) => void) {
      listeners.add(listener)
      refAdded()
      let active = true
      return () => {
        if (!active) return
        active = false
        listeners.delete(listener)
        refRemoved()
      }
    },
    /** Keeps the stream alive (even when the tab is hidden) during a check. */
    acquireLease() {
      leases += 1
      hiddenTimer = clear(hiddenTimer)
      if (paused) {
        paused = false
      }
      refAdded()
      let active = true
      return () => {
        if (!active) return
        active = false
        leases -= 1
        refRemoved()
        if (hidden && leases === 0) scheduleHiddenDisconnect()
      }
    },
    getStatus: () => status,
    failure: () => failureState,
    onStatus(cb: () => void) {
      statusListeners.add(cb)
      return () => {
        statusListeners.delete(cb)
      }
    },
    setHidden(nextHidden: boolean) {
      hidden = nextHidden
      hiddenTimer = clear(hiddenTimer)
      if (!hidden) {
        if (paused) {
          paused = false
          connect()
        }
        return
      }
      scheduleHiddenDisconnect()
    },
  }

  function scheduleHiddenDisconnect() {
    hiddenTimer = clear(hiddenTimer)
    hiddenTimer = timers.setTimeout(() => {
      hiddenTimer = null
      if (hidden && leases === 0 && refs() > 0) {
        paused = true
        disconnect()
      }
    }, DNS_EVENT_HIDDEN_DISCONNECT_MS)
  }
}

let sharedHub: DnsEventHub | null = null

/** Per-tab singleton. */
export function getDnsEventHub(): DnsEventHub {
  if (sharedHub) return sharedHub
  const hub = createDnsEventHub()
  sharedHub = hub
  if (typeof document !== "undefined") {
    const sync = () => hub.setHidden(document.visibilityState === "hidden")
    document.addEventListener("visibilitychange", sync)
    sync()
  }
  return hub
}

export function parseDnsCheckEvent(
  data: string
): ({ type: "HELLO" } | DnsTestInterceptEvent) | null {
  if (!data.trim()) {
    return null
  }

  try {
    const parsed = JSON.parse(data) as Record<string, unknown>
    if (
      !parsed ||
      typeof parsed !== "object" ||
      typeof parsed.type !== "string"
    ) {
      return null
    }
    if (parsed.type === "HELLO") {
      return { type: "HELLO" }
    }
    if (
      parsed.type !== "INTERCEPT" ||
      typeof parsed.domain !== "string" ||
      typeof parsed.source !== "string" ||
      !Object.values(DnsTestInterceptEventSource).includes(
        parsed.source as DnsTestInterceptEventSource
      )
    ) {
      return null
    }
    return {
      type: "INTERCEPT",
      seq: typeof parsed.seq === "number" ? parsed.seq : 0,
      ts_ms: typeof parsed.ts_ms === "number" ? parsed.ts_ms : 0,
      source: parsed.source as DnsTestInterceptEvent["source"],
      domain: parsed.domain,
      lists: Array.isArray(parsed.lists)
        ? parsed.lists.filter(
            (list): list is string => typeof list === "string"
          )
        : [],
      ips: Array.isArray(parsed.ips)
        ? parsed.ips.filter((ip): ip is string => typeof ip === "string")
        : [],
      added: typeof parsed.added === "number" ? parsed.added : 0,
      refreshed: typeof parsed.refreshed === "number" ? parsed.refreshed : 0,
      errors: typeof parsed.errors === "number" ? parsed.errors : 0,
      hold_us: typeof parsed.hold_us === "number" ? parsed.hold_us : 0,
      timed_out: parsed.timed_out === true,
    }
  } catch {
    return null
  }
}
