import { describe, expect, test } from "bun:test"

import {
  createDnsEventHub,
  type ConsumeSse,
  type HubTimers,
} from "../src/api/dns-event-hub"
import { SseHttpError } from "../src/api/authenticated-sse"

function fakeTimers() {
  let now = 0
  let nextId = 1
  const pending = new Map<number, { at: number; fn: () => void }>()
  const timers: HubTimers = {
    setTimeout: (fn, ms) => {
      const id = nextId++
      pending.set(id, { at: now + ms, fn })
      return id
    },
    clearTimeout: (handle) => {
      pending.delete(handle as number)
    },
  }
  const advance = (ms: number) => {
    const target = now + ms
    for (;;) {
      let best: [number, { at: number; fn: () => void }] | null = null
      for (const entry of pending) {
        if (entry[1].at <= target && (!best || entry[1].at < best[1].at))
          best = entry
      }
      if (!best) break
      pending.delete(best[0])
      now = best[1].at
      best[1].fn()
    }
    now = target
  }
  return { timers, advance }
}

type Conn = {
  signal: AbortSignal
  open: () => void
  emit: (data: unknown) => void
  end: () => void
  fail: (error: unknown) => void
}

function fakeConsume() {
  const conns: Conn[] = []
  const consume: ConsumeSse = (_url, signal, onMessage, onOpen) =>
    new Promise<void>((resolve, reject) => {
      conns.push({
        signal,
        open: () => onOpen?.(),
        emit: (data) =>
          onMessage({ event: "message", data: JSON.stringify(data) }),
        end: () => resolve(),
        fail: (error) => reject(error),
      })
    })
  return { conns, consume }
}

const intercept = {
  type: "INTERCEPT",
  domain: "a.check.keen.pbr",
  source: "marker",
}
const flush = () => new Promise((r) => setTimeout(r, 0))

function setup() {
  const t = fakeTimers()
  const c = fakeConsume()
  const hub = createDnsEventHub({ consume: c.consume, timers: t.timers })
  return { hub, ...t, ...c }
}

describe("dns event hub", () => {
  test("three subscribers share one connection and all get events", () => {
    const { hub, conns } = setup()
    const got: string[] = []
    const offs = [1, 2, 3].map((n) =>
      hub.subscribe((e) => got.push(`${n}:${e.domain}`))
    )
    expect(conns.length).toBe(1)
    conns[0].open()
    expect(hub.getStatus()).toBe("connected")
    conns[0].emit(intercept)
    expect(got.length).toBe(3)
    offs.forEach((off) => off())
  })

  test("disconnects 2s after last unsubscribe, not before", () => {
    const { hub, conns, advance } = setup()
    const off = hub.subscribe(() => {})
    conns[0].open()
    off()
    advance(1_900)
    expect(conns[0].signal.aborted).toBe(false)
    advance(200)
    expect(conns[0].signal.aborted).toBe(true)
    expect(hub.getStatus()).toBe("idle")
  })

  test("resubscribing within 2s does not reconnect", () => {
    const { hub, conns, advance } = setup()
    const off = hub.subscribe(() => {})
    conns[0].open()
    off()
    advance(1_000)
    hub.subscribe(() => {})
    advance(5_000)
    expect(conns.length).toBe(1)
    expect(conns[0].signal.aborted).toBe(false)
  })

  test("stream end with subscribers reconnects after 3s", async () => {
    const { hub, conns, advance } = setup()
    hub.subscribe(() => {})
    conns[0].open()
    conns[0].end()
    await flush()
    expect(hub.getStatus()).toBe("error")
    advance(2_900)
    expect(conns.length).toBe(1)
    advance(200)
    expect(conns.length).toBe(2)
    expect(hub.getStatus()).toBe("connecting")
  })

  test("http error is reported with status", async () => {
    const { hub, conns } = setup()
    hub.subscribe(() => {})
    conns[0].fail(new SseHttpError(503))
    await flush()
    expect(hub.failure()).toEqual({ kind: "http", status: 503 })
  })

  test("no headers within 5s marks the stream stalled, then recovers", () => {
    const { hub, conns, advance } = setup()
    hub.subscribe(() => {})
    advance(4_900)
    expect(hub.failure()).toBeNull()
    advance(200)
    expect(hub.failure()).toEqual({ kind: "stalled" })
    expect(hub.getStatus()).toBe("connecting")
    conns[0].open()
    expect(hub.getStatus()).toBe("connected")
    expect(hub.failure()).toBeNull()
  })

  test("hidden for >60s disconnects, visible reconnects; lease blocks it", () => {
    const { hub, conns, advance } = setup()
    hub.subscribe(() => {})
    conns[0].open()
    hub.setHidden(true)
    advance(61_000)
    expect(conns[0].signal.aborted).toBe(true)
    hub.setHidden(false)
    expect(conns.length).toBe(2)

    const release = hub.acquireLease()
    hub.setHidden(true)
    advance(120_000)
    expect(conns[1].signal.aborted).toBe(false)
    release()
    advance(61_000)
    expect(conns[1].signal.aborted).toBe(true)
  })

  test("a lease alone keeps the single connection", () => {
    const { hub, conns } = setup()
    const release = hub.acquireLease()
    hub.subscribe(() => {})
    expect(conns.length).toBe(1)
    release()
  })
})
