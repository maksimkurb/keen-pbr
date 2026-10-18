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
          barSlot: null,
          setBarSlot: () => {},
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

test("mobile bar disables the actions menu at zero selection", () => {
  const html = renderToolbar(0)
  expect(html).toContain('data-testid="bulk-selection-toolbar"')
  expect(html).toContain("fixed inset-x-0 top-0")
  expect(html).toContain("md:hidden")
  expect(html).toContain("flex-nowrap")
  expect(html).toContain('aria-label="common.selection.selectAll"')
  expect(html).toContain('aria-label="common.cancel"')
  expect(html).toContain('aria-label="common.rowActions"')
})

test("mobile bar is not rendered while inactive", () => {
  const html = renderToStaticMarkup(
    <I18nextProvider i18n={i18n}>
      <BulkSelectionToolbar
        active={false}
        countLabel="Selected: 0"
        selection={{
          barSlot: null,
          setBarSlot: () => {},
          selectedIds: new Set(),
          selectedCount: 0,
          totalCount: 15,
          hasSelection: false,
          isSelecting: false,
          allVisibleSelected: false,
          startSelecting: () => {},
          toggleOne: () => {},
          setAllVisible: () => {},
          clear: () => {},
        }}
      >
        <Button onClick={() => {}}>Delete</Button>
      </BulkSelectionToolbar>
    </I18nextProvider>
  )
  expect(html).toBe("")
})
