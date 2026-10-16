/* eslint-disable react-refresh/only-export-components */
import { createContext, useContext, useEffect, useMemo, useState } from "react"

import { getDevicePageTitle } from "@/auth/device-name"
import { authenticatedFetch } from "@/api/client"
import {
  getStreamAuthState,
  subscribeStreamAuthState,
} from "@/api/status-auth"
import { getStatusConnectionState } from "@/api/status-connection"

type AuthState = {
  enabled: boolean
  authenticated: boolean
  deviceName: string
  loading: boolean
  login: (password: string) => Promise<void>
  logout: () => Promise<void>
}

const AuthContext = createContext<AuthState | null>(null)
const tokenKey = "keen-pbr-auth-token"

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [state, setState] = useState({ enabled: false, authenticated: false, deviceName: "", loading: true })

  useEffect(() => {
    const check = async () => {
      const token = sessionStorage.getItem(tokenKey)
      const response = await fetch("/api/auth/status", {
        headers: token ? { Authorization: `Bearer ${token}` } : undefined,
      })
      const body = (await response.json()) as { enabled: boolean; authenticated: boolean; device_name: string }
      if (!body.enabled || !body.authenticated) sessionStorage.removeItem(tokenKey)
      setState({ enabled: body.enabled, authenticated: body.authenticated, deviceName: body.device_name, loading: false })
    }
    // A 401 means authentication is enabled, even if the last status check
    // (or a stream opened before it got enabled) saw it disabled.
    const requireAuth = () => setState((current) => ({ ...current, enabled: true, authenticated: false, loading: false }))
    void check().catch(() => setState({ enabled: false, authenticated: false, deviceName: "", loading: false }))
    // While the status stream is live it pushes the enabled flag and the device
    // name; the poll only runs as the fallback.
    const interval = window.setInterval(() => {
      if (getStatusConnectionState() === "live") return
      void check().catch(() => {})
    }, 60_000)
    const unsubscribeStream = subscribeStreamAuthState(() => {
      const pushed = getStreamAuthState()
      if (!pushed) return
      setState((current) =>
        current.enabled === pushed.enabled &&
        current.deviceName === pushed.deviceName
          ? current
          : { ...current, enabled: pushed.enabled, deviceName: pushed.deviceName }
      )
    })
    const onFocus = () => void check().catch(() => {})
    const onVisibilityChange = () => {
      if (document.visibilityState === "visible") onFocus()
    }
    window.addEventListener("focus", onFocus)
    document.addEventListener("visibilitychange", onVisibilityChange)
    window.addEventListener("keen-pbr-auth-required", requireAuth)
    return () => {
      window.clearInterval(interval)
      unsubscribeStream()
      window.removeEventListener("focus", onFocus)
      document.removeEventListener("visibilitychange", onVisibilityChange)
      window.removeEventListener("keen-pbr-auth-required", requireAuth)
    }
  }, [])

  useEffect(() => {
    document.title = getDevicePageTitle(state.deviceName)
  }, [state.deviceName])

  const value = useMemo<AuthState>(() => ({
    ...state,
    login: async (password) => {
      const response = await fetch("/api/auth/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ password }),
      })
      if (!response.ok) throw new Error(response.status === 401 ? "invalid_credentials" : "login_failed")
      const body = (await response.json()) as { token: string }
      sessionStorage.setItem(tokenKey, body.token)
      setState((current) => ({ ...current, enabled: true, authenticated: true, loading: false }))
    },
    logout: async () => {
      try {
        await authenticatedFetch("/api/auth/logout", { method: "POST" })
      } finally {
        sessionStorage.removeItem(tokenKey)
        setState((current) => ({ ...current, enabled: true, authenticated: false, loading: false }))
      }
    },
  }), [state])

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>
}

export function useAuth() {
  const value = useContext(AuthContext)
  if (!value) throw new Error("useAuth must be used inside AuthProvider")
  return value
}
