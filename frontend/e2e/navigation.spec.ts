import { expect, test } from "@playwright/test"

import { installAppApiMocks } from "./api-mocks"

test.describe("404 & sidebar navigation", () => {
  test("unknown path shows not-found panel and dashboard button works", async ({
    page,
  }) => {
    await installAppApiMocks(page)
    await page.goto("/_e2e_does-not-exist_")

    const block = page.getByTestId("not-found-page")
    await expect(block).toBeVisible()
    await expect(block.getByText("Page not found")).toBeVisible()
    await expect(block.getByText("/_e2e_does-not-exist_")).toBeVisible()

    await block
      .getByRole("button", {
        name: /go to dashboard|на дашборд/i,
      })
      .click()
    await expect(page).toHaveURL("/")

    await expect(page.getByTestId("overview-interface-inventory")).toBeVisible()
  })

  test("routing rules nav stays current on nested /routing-rules/create", async ({
    page,
  }) => {
    await installAppApiMocks(page)
    await page.goto("/routing-rules/create")

    await expect(page).toHaveURL("/routing-rules/create")

    const rulesNav = page.locator('[data-nav-item="/routing-rules"]')
    await expect(rulesNav).toHaveAttribute("aria-current", "page")

    await expect(page.locator('[data-nav-item="/"]')).not.toHaveAttribute(
      "aria-current",
      "page",
    )

    const activeSub = page.locator(
      '[data-slot="sidebar-menu-sub-button"][aria-current="page"]',
    )
    await expect(activeSub).toHaveCount(1)
    await expect(activeSub).toContainText(/routing rules|маршрутизации/i)
  })
})
