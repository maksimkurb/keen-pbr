import { expect, mock, test } from "bun:test"
import { scrollAndHighlightRouteTarget } from "../src/components/layout/scroll-route"

test("route target scrolls to its setting and pulses three times, respecting reduced motion", () => {
  const originalWindow = globalThis.window
  let reducedMotion = false
  Object.defineProperty(globalThis, "window", {
    configurable: true,
    value: { matchMedia: () => ({ matches: reducedMotion }) },
  })
  try {
    const animation = { cancel: mock() }
    const field = { scrollIntoView: mock(), animate: mock(() => animation) }
    const target = { closest: () => field } as unknown as HTMLElement
    expect(scrollAndHighlightRouteTarget(target)).toBe(animation as Animation)
    expect(field.scrollIntoView).toHaveBeenCalledWith({
      behavior: "auto",
      block: "center",
    })
    expect(field.animate.mock.calls[0]?.[1]).toEqual({
      duration: 900,
      iterations: 3,
      easing: "ease-in-out",
    })
    reducedMotion = true
    expect(scrollAndHighlightRouteTarget(target)).toBeUndefined()
    expect(field.scrollIntoView).toHaveBeenCalledTimes(2)
    expect(field.animate).toHaveBeenCalledTimes(1)
  } finally {
    if (originalWindow === undefined) {
      Reflect.deleteProperty(globalThis, "window")
    } else {
      Object.defineProperty(globalThis, "window", {
        configurable: true,
        value: originalWindow,
      })
    }
  }
})
