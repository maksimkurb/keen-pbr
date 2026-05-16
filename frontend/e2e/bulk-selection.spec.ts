import { expect, test } from "@playwright/test"

import { installAppApiMocks, resetAppApiMocks } from "./api-mocks"

test.describe("Bulk row selection", () => {
  test.beforeEach(() => {
    resetAppApiMocks()
  })

  test("lists page shows bulk toolbar after selecting a row", async ({ page }) => {
    await installAppApiMocks(page)
    await page.goto("/lists")

    await expect(page.getByRole("heading", { level: 1 })).toBeVisible()

    const rowCheckboxes = page.locator("tbody").getByRole("checkbox")
    await expect(rowCheckboxes.first()).toBeVisible()
    await rowCheckboxes.first().click()

    const toolbar = page.getByTestId("bulk-selection-toolbar")
    await expect(toolbar).toBeVisible()
    await expect(page.getByTestId("bulk-selection-count")).toHaveText("1 selected")
    await expect(
      page.getByRole("button", {
        name: /delete selected lists/i,
      }),
    ).toBeVisible()
  })

  test("routing rules page shows bulk actions after selecting a rule", async ({
    page,
  }) => {
    await installAppApiMocks(page)
    await page.goto("/routing-rules")

    const rowCheckboxes = page.locator("tbody").getByRole("checkbox")
    await expect(rowCheckboxes.first()).toBeVisible()
    await rowCheckboxes.first().click()

    await expect(page.getByTestId("bulk-selection-toolbar")).toBeVisible()
    await expect(page.getByTestId("bulk-selection-count")).toHaveText("1 selected")
    await expect(page.getByRole("button", { name: /^enable selected$/i })).toBeVisible()
    await expect(page.getByRole("button", { name: /^disable selected$/i })).toBeVisible()
  })

  test("dns rules page shows bulk actions after selecting a rule", async ({
    page,
  }) => {
    await installAppApiMocks(page)
    await page.goto("/dns-rules")

    const rowCheckboxes = page.locator("tbody").getByRole("checkbox")
    await expect(rowCheckboxes.first()).toBeVisible()
    await rowCheckboxes.first().click()

    await expect(page.getByTestId("bulk-selection-toolbar")).toBeVisible()
    await expect(page.getByRole("button", { name: /^disable selected$/i })).toBeVisible()
  })

  test("outbounds page shows bulk delete after selecting a row", async ({
    page,
  }) => {
    await installAppApiMocks(page)
    await page.goto("/outbounds")

    const rowCheckboxes = page.locator("tbody").getByRole("checkbox")
    await expect(rowCheckboxes.first()).toBeVisible()
    await rowCheckboxes.first().click()

    await expect(page.getByTestId("bulk-selection-toolbar")).toBeVisible()
    await expect(page.getByRole("button", { name: /delete selected/i })).toBeVisible()
  })
})
