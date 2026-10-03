"use client"

import { useCallback, useEffect, useRef, useState } from "react"
import { consumeAuthenticatedSse } from "@/api/authenticated-sse"
import {
  DnsTestInterceptEventSource,
  type DnsTestInterceptEvent,
} from "@/api/generated/model"

export type DnsCheckStatus =
  | "idle"
  | "checking"
  | "success"
  | "browser-fail"
  | "sse-fail"
  | "pc-success"

export type InterceptMonitorStatus =
  | "disabled"
  | "connecting"
  | "connected"
  | "error"

type DnsCheckState = {
  randomString: string
  waiting: boolean
  showWarning: boolean
}

type DnsCheckEvent = { type: "HELLO" } | DnsTestInterceptEvent

type UseDnsCheckReturn = {
  status: DnsCheckStatus
  checkState: DnsCheckState
  lastEvent: DnsTestInterceptEvent | null
  startCheck: (performBrowserRequest: boolean) => void
  reset: () => void
}

type UseInterceptEventMonitorReturn = {
  lastEvent: DnsTestInterceptEvent | null
  status: InterceptMonitorStatus
}

export const DNS_CHECK_DOMAIN_SUFFIX = "check.keen.pbr"

const browserCheckTimeoutMs = 5_000
const pcCheckTimeoutMs = 300_000
const pcWarningTimeoutMs = 30_000

export function useDnsCheck(
  markerDomain = DNS_CHECK_DOMAIN_SUFFIX
): UseDnsCheckReturn {
  const eventSourceRef = useRef<AbortController | null>(null)
  const fetchControllerRef = useRef<AbortController | null>(null)
  const checkTimeoutRef = useRef<number | null>(null)
  const warningTimeoutRef = useRef<number | null>(null)
  const checkGenerationRef = useRef(0)

  const [status, setStatus] = useState<DnsCheckStatus>("idle")
  const [lastEvent, setLastEvent] = useState<DnsTestInterceptEvent | null>(null)
  const [checkState, setCheckState] = useState<DnsCheckState>({
    randomString: "",
    waiting: false,
    showWarning: false,
  })

  const cleanup = useCallback(() => {
    checkGenerationRef.current += 1
    if (eventSourceRef.current) {
      eventSourceRef.current.abort()
      eventSourceRef.current = null
    }

    if (fetchControllerRef.current) {
      fetchControllerRef.current.abort()
      fetchControllerRef.current = null
    }

    if (checkTimeoutRef.current !== null) {
      window.clearTimeout(checkTimeoutRef.current)
      checkTimeoutRef.current = null
    }

    if (warningTimeoutRef.current !== null) {
      window.clearTimeout(warningTimeoutRef.current)
      warningTimeoutRef.current = null
    }
  }, [])

  useEffect(() => cleanup, [cleanup])

  const startCheck = useCallback(
    (performBrowserRequest: boolean) => {
      cleanup()

      const generation = checkGenerationRef.current
      const configuredDomain = normalizeDnsMarkerDomain(markerDomain)

      const randomString = Math.random().toString(36).slice(2, 15)
      const domain = `${randomString}.${configuredDomain}`

      setCheckState({
        randomString,
        waiting: !performBrowserRequest,
        showWarning: false,
      })
      setStatus("checking")
      setLastEvent(null)

      if (!performBrowserRequest) {
        warningTimeoutRef.current = window.setTimeout(() => {
          if (generation !== checkGenerationRef.current) {
            return
          }
          setCheckState((current) => ({ ...current, showWarning: true }))
        }, pcWarningTimeoutMs)
      }

      const eventSource = new AbortController()
      eventSourceRef.current = eventSource

      let sseConnected = false

      void consumeAuthenticatedSse(
        "/api/dns/test",
        eventSource.signal,
        ({ data }) => {
          if (generation !== checkGenerationRef.current) {
            return
          }

          const payload = parseDnsCheckEvent(data)
          if (!payload) {
            return
          }

          if (payload.type === "HELLO") {
            sseConnected = true

            if (performBrowserRequest) {
              fetchControllerRef.current = new AbortController()
              fetch(`https://${domain}`, {
                signal: fetchControllerRef.current.signal,
                mode: "no-cors",
              }).catch((error: unknown) => {
                if (
                  error &&
                  typeof error === "object" &&
                  "name" in error &&
                  error.name === "AbortError"
                ) {
                  return
                }
              })
            }

            return
          }

          if (payload.type !== "INTERCEPT") {
            return
          }

          setLastEvent(payload)

          if (
            payload.source !== "marker" ||
            payload.domain.toLowerCase() !== domain
          ) {
            return
          }

          cleanup()
          setCheckState((current) => ({
            ...current,
            waiting: false,
            showWarning: false,
          }))
          setStatus(performBrowserRequest ? "success" : "pc-success")
        }
      ).catch(() => {
        /* Let the timeout report the failure. */
      })

      checkTimeoutRef.current = window.setTimeout(
        () => {
          if (generation !== checkGenerationRef.current) {
            return
          }
          cleanup()

          if (!sseConnected) {
            setStatus("sse-fail")
            return
          }

          if (performBrowserRequest) {
            setStatus("browser-fail")
            return
          }

          setCheckState((current) => ({
            ...current,
            waiting: false,
            showWarning: true,
          }))
        },
        performBrowserRequest ? browserCheckTimeoutMs : pcCheckTimeoutMs
      )
    },
    [cleanup, markerDomain]
  )

  const reset = useCallback(() => {
    cleanup()
    setStatus("idle")
    setLastEvent(null)
    setCheckState({
      randomString: "",
      waiting: false,
      showWarning: false,
    })
  }, [cleanup])

  return {
    status,
    checkState,
    lastEvent,
    startCheck,
    reset,
  }
}

export function parseDnsCheckEvent(data: string): DnsCheckEvent | null {
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

export function useInterceptEventMonitor(
  enabled: boolean
): UseInterceptEventMonitorReturn {
  const controllerRef = useRef<AbortController | null>(null)
  const generationRef = useRef(0)
  const [status, setStatus] = useState<InterceptMonitorStatus>(
    enabled ? "connecting" : "disabled"
  )
  const [lastEvent, setLastEvent] = useState<DnsTestInterceptEvent | null>(null)

  useEffect(() => {
    const generation = generationRef.current + 1
    generationRef.current = generation
    controllerRef.current?.abort()
    controllerRef.current = null
    queueMicrotask(() => {
      if (generation !== generationRef.current) {
        return
      }
      setLastEvent(null)
      setStatus(enabled ? "connecting" : "disabled")
    })

    if (!enabled) {
      return
    }

    const controller = new AbortController()
    controllerRef.current = controller

    void consumeAuthenticatedSse(
      "/api/dns/test",
      controller.signal,
      ({ data }) => {
        if (generation !== generationRef.current || controller.signal.aborted) {
          return
        }

        const payload = parseDnsCheckEvent(data)
        if (!payload) {
          return
        }

        if (payload.type === "HELLO") {
          setStatus("connected")
          return
        }

        setLastEvent(payload)
      }
    )
      .then(() => {
        if (
          generation === generationRef.current &&
          !controller.signal.aborted
        ) {
          setStatus("error")
        }
      })
      .catch(() => {
        if (
          generation === generationRef.current &&
          !controller.signal.aborted
        ) {
          setStatus("error")
        }
      })

    return () => {
      generationRef.current += 1
      controller.abort()
      if (controllerRef.current === controller) {
        controllerRef.current = null
      }
    }
  }, [enabled])

  return { lastEvent, status }
}

export function normalizeDnsMarkerDomain(value: string) {
  const normalized = value.trim().replace(/\.+$/, "").toLowerCase()
  return normalized || DNS_CHECK_DOMAIN_SUFFIX
}
