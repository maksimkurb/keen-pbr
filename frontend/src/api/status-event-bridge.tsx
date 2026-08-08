import { useEffect } from "react"
import { useQueryClient } from "@tanstack/react-query"

import { applyStatusEvent } from "@/api/status-event-cache"
import { consumeAuthenticatedSse } from "@/api/authenticated-sse"

const HIDDEN_DISCONNECT_DELAY_MS = 60_000

export function StatusEventBridge() {
  const queryClient = useQueryClient()

  useEffect(() => {
    let source: AbortController | null = null
    let hiddenTimer: ReturnType<typeof setTimeout> | null = null
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null

    const connect = () => {
      if (source !== null) return
      source = new AbortController()
      const controller = source
      void consumeAuthenticatedSse("/api/status/events", controller.signal, ({ event, data }) => {
        if (["snapshot", "service", "outbounds", "interfaces"].includes(event)) applyStatusEvent(queryClient, data)
      }).catch(() => {
        if (!controller.signal.aborted) {
          source = null
          reconnectTimer = setTimeout(connect, 3_000)
        }
      })
    }

    const disconnect = () => {
      source?.abort()
      source = null
      if (reconnectTimer !== null) clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
    const onVisibilityChange = () => {
      if (hiddenTimer !== null) clearTimeout(hiddenTimer)
      hiddenTimer = null
      if (document.visibilityState === "visible") {
        connect()
      } else {
        hiddenTimer = setTimeout(disconnect, HIDDEN_DISCONNECT_DELAY_MS)
      }
    }

    connect()
    onVisibilityChange()
    document.addEventListener("visibilitychange", onVisibilityChange)
    return () => {
      document.removeEventListener("visibilitychange", onVisibilityChange)
      if (hiddenTimer !== null) clearTimeout(hiddenTimer)
      disconnect()
    }
  }, [queryClient])

  return null
}
