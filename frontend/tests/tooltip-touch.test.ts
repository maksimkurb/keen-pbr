import { describe, expect, test } from "bun:test"

import { TooltipTouchGesture } from "../src/components/ui/tooltip-touch"

const waitForHold = () => new Promise((resolve) => setTimeout(resolve, 550))

describe("tooltip touch gestures", () => {
  test("a passive tap toggles the hint and suppresses its click", () => {
    const calls: boolean[] = []
    const gesture = new TooltipTouchGesture()
    gesture.start(1, 0, 0, false, (toggle) => calls.push(toggle))
    gesture.end(1)
    expect(calls).toEqual([true])
    expect(gesture.suppressClick).toBe(true)
    expect(gesture.consumeClick(0)).toBe(false)
    expect(gesture.consumeClick(1)).toBe(true)
    expect(gesture.consumeClick(1)).toBe(false)
  })

  test("a short press on an action leaves the click alone", () => {
    const calls: boolean[] = []
    const gesture = new TooltipTouchGesture()
    gesture.start(1, 0, 0, true, (toggle) => calls.push(toggle))
    gesture.end(1)
    expect(calls).toEqual([])
    expect(gesture.suppressClick).toBe(false)
  })

  test("a different pointer cannot finish a tap", () => {
    const calls: boolean[] = []
    const gesture = new TooltipTouchGesture()
    gesture.start(1, 0, 0, false, (toggle) => calls.push(toggle))
    gesture.end(2)
    expect(calls).toEqual([])
    gesture.end(1)
    expect(calls).toEqual([true])
  })

  test("a long press shows the hint once and suppresses the action", async () => {
    const calls: boolean[] = []
    const gesture = new TooltipTouchGesture()
    gesture.start(1, 0, 0, true, (toggle) => calls.push(toggle))
    await waitForHold()
    expect(gesture.contextMenu()).toBe(true)
    gesture.end(1)
    expect(calls).toEqual([false])
    expect(gesture.suppressClick).toBe(true)
    gesture.start(2, 0, 0, true, (toggle) => calls.push(toggle))
    gesture.end(2)
    expect(gesture.suppressClick).toBe(false)
  })

  test("movement, cancellation and release cancel pending long presses", async () => {
    const calls: boolean[] = []
    const moved = new TooltipTouchGesture()
    const canceled = new TooltipTouchGesture()
    const released = new TooltipTouchGesture()
    moved.start(1, 0, 0, true, (toggle) => calls.push(toggle))
    moved.move(1, 11, 0)
    canceled.start(1, 0, 0, true, (toggle) => calls.push(toggle))
    canceled.cancel()
    released.start(1, 0, 0, true, (toggle) => calls.push(toggle))
    released.end(1)
    await waitForHold()
    expect(calls).toEqual([])
    expect(moved.contextMenu()).toBe(false)
    expect(canceled.suppressClick).toBe(false)

    moved.start(2, 0, 0, false, (toggle) => calls.push(toggle))
    moved.move(2, 11, 0)
    moved.end(2)
    expect(calls).toEqual([])
  })

  test("a native touch context menu opens the hint and is suppressed", () => {
    const calls: boolean[] = []
    const gesture = new TooltipTouchGesture()
    gesture.start(1, 0, 0, true, (toggle) => calls.push(toggle))
    expect(gesture.contextMenu()).toBe(true)
    gesture.end(1)
    expect(calls).toEqual([false])
    expect(gesture.suppressClick).toBe(true)
  })
})
