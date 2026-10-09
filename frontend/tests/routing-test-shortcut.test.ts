import { expect, test } from "bun:test"
import { isRoutingTestShortcut } from "../src/components/overview/routing-test-shortcut"

const shortcut = {
  ctrlKey: true,
  altKey: true,
  shiftKey: false,
  metaKey: false,
  repeat: false,
  isComposing: false,
  defaultPrevented: false,
  code: "KeyK",
  key: "k",
} as KeyboardEvent

test("routing shortcut works with Latin and Russian layouts", () => {
  expect(isRoutingTestShortcut(shortcut)).toBe(true)
  expect(isRoutingTestShortcut({ ...shortcut, key: "л" })).toBe(true)
})

test("routing shortcut ignores other keys, modifiers and repeated or handled input", () => {
  for (const override of [
    { ctrlKey: false },
    { altKey: false },
    { shiftKey: true },
    { metaKey: true },
    { repeat: true },
    { isComposing: true },
    { defaultPrevented: true },
    { code: "KeyJ", key: "j" },
  ]) {
    expect(isRoutingTestShortcut({ ...shortcut, ...override })).toBe(false)
  }
})
