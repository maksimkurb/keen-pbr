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

    await expect(
      page.getByText(/^\d+ selected$|^\d+ выбрано$/i),
    ).toBeVisible()
    await expect(
      page.getByRole("button", {
        name: /delete selected lists|удалить выбранные списки/i,
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

    await expect(page.getByText(/^\d+ selected$|^\d+ выбрано$/i)).toBeVisible()
    await expect(
      page.getByRole("button", { name: /enable|включить/i }),
    ).toBeVisible()
    await expect(
      page.getByRole("button", { name: /disable|отключить/i }),
    ).toBeVisible()
  })
})
