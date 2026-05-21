import { expect, test } from "@playwright/test"

import { installAppApiMocks } from "./api-mocks"

test.describe("Overview interface inventory", () => {
  test("shows polled interface inventory from /api/runtime/interfaces", async ({
    page,
  }) => {
    await installAppApiMocks(page)
    await page.goto("/")

    const block = page.getByTestId("overview-interface-inventory")
    await expect(block.getByText("e2e_wan")).toBeVisible()
    await expect(
      block.locator('[data-testid="overview-interface-row"]'),
    ).toHaveCount(1)
    await expect(block.getByText("192.0.2.10/24")).toBeVisible()
    await expect(block.getByText(/\+.*more|\bещё\b/i)).toBeVisible()
  })
})

test.describe("Routing rule list usage hints", () => {
  test("list picker subtitle names the other routing rules using the list", async ({
    page,
  }) => {
    await installAppApiMocks(page)
    await page.goto("/routing-rules/create")

    const listField = page.locator('[data-field-name="list"]')
    await listField.getByRole("combobox").click()

    const option = page.locator('[data-list-option="shared_list"]')
    // The hint is intentionally concise: rule number + outbound, no criteria dump.
    await expect(option).toContainText("#1 → wan0")
    await expect(option).toContainText("#2 → drop")
  })
})
