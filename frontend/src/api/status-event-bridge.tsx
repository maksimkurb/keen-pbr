import { useEffect } from "react"
import { useQueryClient } from "@tanstack/react-query"

import { applyStatusEvent } from "@/api/status-event-cache"

const HIDDEN_DISCONNECT_DELAY_MS = 60_000

export function StatusEventBridge() {
  const queryClient = useQueryClient()

  useEffect(() => {
    let source: EventSource | null = null
    let hiddenTimer: ReturnType<typeof setTimeout> | null = null

    const connect = () => {
      if (source !== null) return
      source = new EventSource("/api/status/events")
      source.addEventListener("snapshot", (event) => {
        applyStatusEvent(queryClient, (event as MessageEvent).data)
      })
      source.addEventListener("service", (event) => {
        applyStatusEvent(queryClient, (event as MessageEvent).data)
      })
      source.addEventListener("outbounds", (event) => {
        applyStatusEvent(queryClient, (event as MessageEvent).data)
      })
      source.addEventListener("interfaces", (event) => {
        applyStatusEvent(queryClient, (event as MessageEvent).data)
      })
    }

    const disconnect = () => {
      source?.close()
      source = null
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
