export function isRoutingTestShortcut(event: KeyboardEvent) {
  return (
    event.ctrlKey &&
    event.altKey &&
    !event.shiftKey &&
    !event.metaKey &&
    !event.repeat &&
    !event.isComposing &&
    !event.defaultPrevented &&
    (event.code === "KeyK" || event.key.toLowerCase() === "k")
  )
}
