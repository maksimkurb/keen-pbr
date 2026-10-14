import { useEffect, useRef } from "react"
import { useQueryClient } from "@tanstack/react-query"

import {
  applyStatusEvent,
  getTerminalConfigLifecycleOperationKey,
  invalidateAfterTerminalConfigOperation,
  statusStreamQueryKeys,
} from "@/api/status-event-cache"
import {
  markStatusQueriesStale,
  setStatusConnectionState,
} from "@/api/status-connection"
import { consumeAuthenticatedSse } from "@/api/authenticated-sse"

const HIDDEN_DISCONNECT_DELAY_MS = 60_000

export function StatusEventBridge() {
  const queryClient = useQueryClient()
  const lastConfigResyncOperationRef = useRef<string | null>(null)

  useEffect(() => {
    let source: AbortController | null = null
    let hiddenTimer: ReturnType<typeof setTimeout> | null = null
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null

    const markDown = () => {
      setStatusConnectionState("down")
      markStatusQueriesStale(queryClient, statusStreamQueryKeys())
    }

    const settle = (controller: AbortController) => {
      if (source !== controller || controller.signal.aborted) return

      source = null
      markDown()
      if (reconnectTimer !== null) clearTimeout(reconnectTimer)
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null
        connect()
      }, 3_000)
    }

    const connect = () => {
      if (source !== null) return
      if (reconnectTimer !== null) {
        clearTimeout(reconnectTimer)
        reconnectTimer = null
      }
      source = new AbortController()
      setStatusConnectionState("connecting")
      const controller = source
      void consumeAuthenticatedSse(
        "/api/status/events",
        controller.signal,
        ({ event, data }) => {
          if (
            !["snapshot", "service", "outbounds", "interfaces"].includes(event)
          ) {
            return
          }

          applyStatusEvent(queryClient, data)
          if (event === "snapshot") setStatusConnectionState("live")
          const terminalOperationKey =
            getTerminalConfigLifecycleOperationKey(data)
          if (
            terminalOperationKey &&
            terminalOperationKey !== lastConfigResyncOperationRef.current
          ) {
            lastConfigResyncOperationRef.current = terminalOperationKey
            invalidateAfterTerminalConfigOperation(queryClient)
          }
        }
      ).then(
        () => settle(controller),
        () => settle(controller)
      )
    }

    const disconnect = () => {
      if (source !== null) markDown()
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
