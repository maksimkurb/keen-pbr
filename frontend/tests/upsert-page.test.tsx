import { expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"
import { UpsertPage } from "../src/components/shared/upsert-page"

function renderPage(withCard?: boolean) {
  return renderToStaticMarkup(
    <UpsertPage
      title="Изменить список cubly_ru"
      description="Description"
      cardTitle="Card title"
      cardDescription="Card description"
      withCard={withCard}
    >
      <form>
        <input aria-label="Name" defaultValue="cubly_ru" />
      </form>
    </UpsertPage>
  )
}

test("upsert page can show an edit form directly below its identifying header", () => {
  const html = renderPage(false)
  expect(html).toContain("Изменить список cubly_ru")
  expect(html).toContain("<form>")
  expect(html).not.toContain('data-slot="card"')
  expect(html).not.toContain("Card title")
})

test("creation omits the outer card by default", () => {
  const html = renderPage()
  expect(html).not.toContain('data-slot="card"')
  expect(html).not.toContain("Card title")
  expect(html).not.toContain("Card description")
  expect(html).toContain("<form>")
})
