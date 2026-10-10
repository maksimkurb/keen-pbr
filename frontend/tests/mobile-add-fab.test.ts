import { expect, test } from "bun:test"
import { getMobileFabCollapsedState } from "../src/components/shared/mobile-add-fab-utils"

test("mobile add FAB changes only after deliberate scroll and follows direction", () => {
  expect(getMobileFabCollapsedState(11, 0)).toBeNull()
  expect(getMobileFabCollapsedState(20, 0)).toBe(false)
  expect(getMobileFabCollapsedState(36, 24)).toBe(true)
  expect(getMobileFabCollapsedState(24, 36)).toBe(false)
  expect(getMobileFabCollapsedState(18, 30)).toBe(false)
})
