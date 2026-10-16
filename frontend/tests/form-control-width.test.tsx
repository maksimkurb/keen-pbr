import { expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"
import { Input } from "../src/components/ui/input"
import { Select, SelectTrigger, SelectValue } from "../src/components/ui/select"

test("short inputs share a width limit and explicit full-width inputs can override it", () => {
  expect(renderToStaticMarkup(<Input type="number" />)).toContain("max-w-sm")
  expect(renderToStaticMarkup(<Input inputMode="numeric" />)).toContain(
    "max-w-sm"
  )
  expect(renderToStaticMarkup(<Input />)).not.toContain("max-w-sm")
  expect(
    renderToStaticMarkup(<Input type="number" className="max-w-none" />)
  ).not.toContain("max-w-sm")
})

test("selects use the same short width and allow full-width selectors", () => {
  const render = (className?: string) =>
    renderToStaticMarkup(
      <Select
        items={[{ value: "normal", label: "Обычное правило" }]}
        value="normal"
      >
        <SelectTrigger className={className}>
          <SelectValue />
        </SelectTrigger>
      </Select>
    )
  expect(render()).toContain("max-w-sm")
  expect(render("max-w-none")).not.toContain("max-w-sm")
  expect(render()).toContain("Обычное правило")
})
