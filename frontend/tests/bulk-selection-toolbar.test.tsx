import { expect, test } from "bun:test"
import { createInstance } from "i18next"
import { I18nextProvider } from "react-i18next"
import { renderToStaticMarkup } from "react-dom/server"
import { BulkSelectionToolbar } from "../src/components/shared/bulk-selection-toolbar"
import { Button } from "../src/components/ui/button"

const i18n = createInstance()
await i18n.init({ lng: "en", resources: {}, fallbackLng: false })

function renderToolbar(count: number) {
  return renderToStaticMarkup(
    <I18nextProvider i18n={i18n}>
      <BulkSelectionToolbar
        countLabel={`Selected: ${count}`}
        selection={{
          selectedIds: new Set(count ? ["row"] : []),
          selectedCount: count,
          totalCount: 15,
          hasSelection: count > 0,
          isSelecting: true,
          allVisibleSelected: false,
          startSelecting: () => {},
          toggleOne: () => {},
          setAllVisible: () => {},
          clear: () => {},
        }}
      >
        <Button onClick={() => {}}>Delete</Button>
        <Button disabled onClick={() => {}}>
          Refresh
        </Button>
      </BulkSelectionToolbar>
    </I18nextProvider>
  )
}

test("bulk toolbar remains present with disabled actions at zero selection", () => {
  const html = renderToolbar(0)
  expect(html).toContain('data-testid="bulk-selection-toolbar"')
  expect(html).toContain("fixed inset-x-0 top-0")
  expect(html).toContain("md:static")
  expect(html).toContain("flex-nowrap")
  expect(html).toContain('aria-label="common.selection.selectAll"')
  expect(html).toContain('aria-label="common.cancel"')
  expect(html).toContain("contents md:block")
  expect(html).toMatch(/<button[^>]* disabled=""[^>]*>Delete<\/button>/)
  expect(html).toContain("common.cancel")
  expect(html).toContain("common.selection.selectAll")
  expect(html).toContain("common.rowActions")
})

test("selection enables actions while preserving per-action disabled state", () => {
  const html = renderToolbar(1)
  expect(html).not.toMatch(/<button[^>]* disabled=""[^>]*>Delete<\/button>/)
  expect(html).toMatch(/<button[^>]* disabled=""[^>]*>Refresh<\/button>/)
  expect(html).toContain("hidden flex-wrap gap-2 md:flex")
})
