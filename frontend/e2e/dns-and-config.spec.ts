import { expect, test } from "@playwright/test"

import {
  getMockApiState,
  installAppApiMocks,
  resetAppApiMocks,
} from "./api-mocks"

test.describe("DNS list usage hints", () => {
  test.beforeEach(() => {
    resetAppApiMocks()
  })

  test("dns rule create shows other rules using the same list", async ({
    page,
  }) => {
    await installAppApiMocks(page)
    await page.goto("/dns-rules/create")

    const listField = page.locator('[data-field-name="rule.lists"]')
    await listField.getByRole("combobox").click()

    const option = page.locator('[data-list-option="shared_list"]')
    await expect(option).toContainText("#1 → upstream")
    await expect(option).toContainText("#2 → backup")
  })
})

test.describe("Bulk config staging", () => {
  test.beforeEach(() => {
    resetAppApiMocks()
  })

  test("bulk disable on routing rules stages config via POST /api/config", async ({
    page,
  }) => {
    await installAppApiMocks(page)
    await page.goto("/routing-rules")

    await page.locator("tbody").getByRole("checkbox").first().click()
    await page.getByRole("button", { name: /^disable selected$/i }).click()

    await expect
      .poll(() => getMockApiState().postConfigCallCount)
      .toBeGreaterThan(0)

    const firstRule = getMockApiState().config.route?.rules?.[0]
    expect(firstRule?.enabled).toBe(false)
  })
})
