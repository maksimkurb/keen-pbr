export type RoutingTestField = "src_addr" | "src_port" | "proto" | "dest_port"

export const ROUTING_TEST_FIELD_IDS: Record<RoutingTestField, string> = {
  src_addr: "routing-test-source-ip",
  src_port: "routing-test-source-port",
  proto: "routing-test-protocol",
  dest_port: "routing-test-port",
}

const FOCUS_EVENT = "routing-test-focus-field"

/** Asks the routing test form to reveal, focus and highlight a field. */
export function focusRoutingTestField(field: RoutingTestField) {
  window.dispatchEvent(new CustomEvent(FOCUS_EVENT, { detail: field }))
}

export function onRoutingTestFocusField(
  handler: (field: RoutingTestField) => void
) {
  const listener = (event: Event) =>
    handler((event as CustomEvent<RoutingTestField>).detail)
  window.addEventListener(FOCUS_EVENT, listener)
  return () => window.removeEventListener(FOCUS_EVENT, listener)
}
