"use client"

import { useCallback, useEffect, useRef, useState } from "react"
import {
  getDnsEventHub,
  parseDnsCheckEvent,
  type DnsEventFailure,
} from "@/api/dns-event-hub"
import type { DnsTestInterceptEvent } from "@/api/generated/model"

export { parseDnsCheckEvent }
export type { DnsEventFailure }

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

type UseDnsCheckReturn = {
  status: DnsCheckStatus
  checkState: DnsCheckState
  lastEvent: DnsTestInterceptEvent | null
  failure: DnsEventFailure | null
  startCheck: (performBrowserRequest: boolean) => void
  reset: () => void
}

type UseInterceptEventMonitorReturn = {
  lastEvent: DnsTestInterceptEvent | null
  status: InterceptMonitorStatus
}

export const DNS_CHECK_DOMAIN_SUFFIX = "check.keen.pbr"

const sseConnectTimeoutMs = 5_000
const browserCheckTimeoutMs = 5_000
const pcCheckTimeoutMs = 300_000
const pcWarningTimeoutMs = 30_000

export function useDnsCheck(
  markerDomain = DNS_CHECK_DOMAIN_SUFFIX
): UseDnsCheckReturn {
  const releaseRef = useRef<(() => void) | null>(null)
  const fetchControllerRef = useRef<AbortController | null>(null)
  const checkTimeoutRef = useRef<number | null>(null)
  const warningTimeoutRef = useRef<number | null>(null)
  const checkGenerationRef = useRef(0)

  const [status, setStatus] = useState<DnsCheckStatus>("idle")
  const [failure, setFailure] = useState<DnsEventFailure | null>(null)
  const [lastEvent, setLastEvent] = useState<DnsTestInterceptEvent | null>(null)
  const [checkState, setCheckState] = useState<DnsCheckState>({
    randomString: "",
    waiting: false,
    showWarning: false,
  })

  const cleanup = useCallback(() => {
    checkGenerationRef.current += 1
    if (releaseRef.current) {
      releaseRef.current()
      releaseRef.current = null
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
      setFailure(null)
      setLastEvent(null)

      if (!performBrowserRequest) {
        warningTimeoutRef.current = window.setTimeout(() => {
          if (generation !== checkGenerationRef.current) {
            return
          }
          setCheckState((current) => ({ ...current, showWarning: true }))
        }, pcWarningTimeoutMs)
      }

      // One shared /api/dns/test stream per tab: hold a lease for the whole
      // check and listen to the hub instead of opening our own connection.
      const hub = getDnsEventHub()
      const releaseLease = hub.acquireLease()
      const unsubscribe = hub.subscribe((payload) => {
        if (generation !== checkGenerationRef.current) {
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
      })
      let offStatus: (() => void) | null = null
      releaseRef.current = () => {
        offStatus?.()
        unsubscribe()
        releaseLease()
      }

      const onConnected = () => {
        if (generation !== checkGenerationRef.current) {
          return
        }
        if (checkTimeoutRef.current !== null) {
          window.clearTimeout(checkTimeoutRef.current)
          checkTimeoutRef.current = null
        }
        offStatus?.()
        offStatus = null

        // The stream is open: the connection itself works. Start the
        // browser lookup now; the marker INTERCEPT event proves the path.
        if (performBrowserRequest) {
          fetchControllerRef.current = new AbortController()
          fetch(`https://${domain}`, {
            signal: fetchControllerRef.current.signal,
            mode: "no-cors",
          }).catch(() => {
            /* Only the observed INTERCEPT event matters. */
          })
        }

        checkTimeoutRef.current = window.setTimeout(
          () => {
            if (generation !== checkGenerationRef.current) {
              return
            }
            cleanup()

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
      }

      if (hub.getStatus() === "connected") {
        onConnected()
        return
      }

      offStatus = hub.onStatus(() => {
        if (hub.getStatus() === "connected") onConnected()
      })
      checkTimeoutRef.current = window.setTimeout(() => {
        checkTimeoutRef.current = null
        if (generation !== checkGenerationRef.current) {
          return
        }
        const hubFailure = hub.failure()
        cleanup()
        setFailure(hubFailure ?? { kind: "network" })
        setStatus("sse-fail")
      }, sseConnectTimeoutMs)
    },
    [cleanup, markerDomain]
  )

  const reset = useCallback(() => {
    cleanup()
    setStatus("idle")
    setFailure(null)
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
    failure,
    startCheck,
    reset,
  }
}

/** Translation key suffix + params for an sse-fail status. */
export function describeSseFailure(failure: DnsEventFailure | null) {
  if (failure?.kind === "stalled") return { key: "sseStalled" }
  if (failure?.kind === "http")
    return { key: "sseHttp", params: { status: failure.status ?? 0 } }
  return { key: null }
}

export function useInterceptEventMonitor(
  enabled: boolean
): UseInterceptEventMonitorReturn {
  const [status, setStatus] = useState<InterceptMonitorStatus>(
    enabled ? "connecting" : "disabled"
  )
  const [lastEvent, setLastEvent] = useState<DnsTestInterceptEvent | null>(null)

  useEffect(() => {
    let cancelled = false
    queueMicrotask(() => {
      if (cancelled) return
      setLastEvent(null)
      setStatus(
        enabled ? mapHubStatus(getDnsEventHub().getStatus()) : "disabled"
      )
    })

    if (!enabled) {
      return
    }

    const hub = getDnsEventHub()
    const offStatus = hub.onStatus(() => {
      if (!cancelled) setStatus(mapHubStatus(hub.getStatus()))
    })
    const unsubscribe = hub.subscribe((event) => {
      if (!cancelled) setLastEvent(event)
    })

    return () => {
      cancelled = true
      offStatus()
      unsubscribe()
    }
  }, [enabled])

  return { lastEvent, status }
}

function mapHubStatus(
  status: ReturnType<ReturnType<typeof getDnsEventHub>["getStatus"]>
): InterceptMonitorStatus {
  return status === "connected"
    ? "connected"
    : status === "error"
      ? "error"
      : "connecting"
}

export function normalizeDnsMarkerDomain(value: string) {
  const normalized = value.trim().replace(/\.+$/, "").toLowerCase()
  return normalized || DNS_CHECK_DOMAIN_SUFFIX
}
