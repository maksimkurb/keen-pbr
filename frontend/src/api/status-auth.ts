// The public auth state (enabled flag and device name) as last pushed by the
// status stream. Read by the auth context, which owns the login flow.
export type StreamAuthState = { enabled: boolean; deviceName: string }

let current: StreamAuthState | null = null
const listeners = new Set<() => void>()

export function getStreamAuthState(): StreamAuthState | null {
  return current
}

export function subscribeStreamAuthState(listener: () => void) {
  listeners.add(listener)
  return () => {
    listeners.delete(listener)
  }
}

export function setStreamAuthState(next: StreamAuthState) {
  if (
    current?.enabled === next.enabled &&
    current.deviceName === next.deviceName
  ) {
    return
  }
  current = next
  for (const listener of [...listeners]) listener()
}
