"use client"

import { useCallback, useEffect, useRef, useState } from "react"

export type DnsCheckStatus =
  | "idle"
  | "checking"
  | "success"
  | "browser-fail"
  | "sse-fail"
  | "pc-success"

type DnsCheckState = {
  randomString: string
  waiting: boolean
  showWarning: boolean
}

type DnsCheckEvent =
  | { type: "HELLO" }
  | { type: "DNS"; domain?: string | null; source_ip?: string | null; ecs?: string | null }

type UseDnsCheckReturn = {
  status: DnsCheckStatus
  checkState: DnsCheckState
  startCheck: (performBrowserRequest: boolean) => void
  reset: () => void
}

const browserCheckTimeoutMs = 5_000
const pcCheckTimeoutMs = 300_000
const pcWarningTimeoutMs = 30_000

export function useDnsCheck(): UseDnsCheckReturn {
  const eventSourceRef = useRef<EventSource | null>(null)
  const fetchControllerRef = useRef<AbortController | null>(null)
  const checkTimeoutRef = useRef<number | null>(null)
  const warningTimeoutRef = useRef<number | null>(null)

  const [status, setStatus] = useState<DnsCheckStatus>("idle")
  const [checkState, setCheckState] = useState<DnsCheckState>({
    randomString: "",
    waiting: false,
    showWarning: false,
  })

  const cleanup = useCallback(() => {
    if (eventSourceRef.current) {
      eventSourceRef.current.close()
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

      const randomString = Math.random().toString(36).slice(2, 15)
      const domain = `${randomString}.dns-check.keen-pbr.internal`

      setCheckState({
        randomString,
        waiting: !performBrowserRequest,
        showWarning: false,
      })
      setStatus("checking")

      if (!performBrowserRequest) {
        warningTimeoutRef.current = window.setTimeout(() => {
          setCheckState((current) => ({ ...current, showWarning: true }))
        }, pcWarningTimeoutMs)
      }

      const eventSource = new EventSource("/api/dns/test")
      eventSourceRef.current = eventSource

      let sseConnected = false

      eventSource.onmessage = (event) => {
        const payload = parseDnsCheckEvent(event.data)
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

        if (payload.type !== "DNS" || payload.domain !== domain) {
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

      eventSource.onerror = () => {
        // Let the timeout decide whether the connection or lookup failed.
      }

      checkTimeoutRef.current = window.setTimeout(
        () => {
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
    [cleanup]
  )

  const reset = useCallback(() => {
    cleanup()
    setStatus("idle")
    setCheckState({
      randomString: "",
      waiting: false,
      showWarning: false,
    })
  }, [cleanup])

  return {
    status,
    checkState,
    startCheck,
    reset,
  }
}

function parseDnsCheckEvent(data: string): DnsCheckEvent | null {
  if (!data.trim()) {
    return null
  }

  try {
    const parsed = JSON.parse(data) as DnsCheckEvent
    if (!parsed || typeof parsed !== "object" || !("type" in parsed)) {
      return null
    }
    return parsed
  } catch {
    return null
  }
}
