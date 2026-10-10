import { describe, expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"

import { DataTable } from "../src/components/shared/data-table"

const rows = [["vpn", "interface", "configuration", "runtime", "actions"]]

describe("DataTable mobile cards", () => {
  test("keeps the table and puts runtime before configuration in a mobile block", () => {
    const html = renderToStaticMarkup(
      <DataTable
        headers={["Name", "Type", "Config", "Now", "Actions"]}
        rows={rows}
        mobileCards={{ titleColumns: [0, 1], bodyColumns: [3, 2] }}
      />
    )
    const card = html.slice(
      html.indexOf("<article"),
      html.indexOf("</article>")
    )
    expect(card).toContain("vpn")
    expect(card).toContain("interface")
    expect(card).toContain("actions")
    expect(card.indexOf("runtime")).toBeLessThan(card.indexOf("configuration"))
    expect(html).toContain("hidden md:block")
    expect(html).toContain("<table")
  })

  test("can hide a mobile field label while keeping its value and table header", () => {
    const html = renderToStaticMarkup(
      <DataTable
        headers={["Name", "Type", "Config", "Now", "Actions"]}
        rows={rows}
        mobileCards={{
          titleColumns: [0, 1],
          bodyColumns: [3, 2],
          hideLabels: [2],
        }}
      />
    )
    const card = html.slice(
      html.indexOf("<article"),
      html.indexOf("</article>")
    )
    expect(card).not.toContain("Config")
    expect(card).toContain("configuration")
    expect(card).toContain("Now")
    expect(html).toContain("Config")
  })

  test("other tables stay tables without mobile opt-in", () => {
    const html = renderToStaticMarkup(<DataTable rows={rows} />)
    expect(html).not.toContain("<article")
    expect(html).not.toContain("hidden md:block")
    expect(html).toContain("<table")
  })

  test("selection mode exposes checkboxes before any row is selected", () => {
    const html = renderToStaticMarkup(
      <DataTable
        rows={rows}
        mobileCards={{ titleColumns: [0, 1], bodyColumns: [3, 2] }}
        selection={{
          rowIds: ["vpn"],
          selectedIds: new Set(),
          isSelecting: true,
          onToggle: () => {},
          onToggleAll: () => {},
          getRowLabel: (id) => `Select ${id}`,
        }}
      />
    )
    const card = html.slice(
      html.indexOf("<article"),
      html.indexOf("</article>")
    )
    expect(card).toContain('aria-label="Select vpn"')
    expect(card).toContain('aria-checked="false"')
    expect(card).toContain('data-selected="false"')
    expect(card).toContain('data-selecting="true"')
    expect(card.match(/inert=""/g)).toHaveLength(3)
    expect(card).toContain('class="invisible"')
    expect(html).toContain("invisible")
  })

  test("selected rows expose mobile checkboxes with the same label and state", () => {
    const html = renderToStaticMarkup(
      <DataTable
        rows={rows}
        mobileCards={{ titleColumns: [0, 1], bodyColumns: [3, 2] }}
        selection={{
          rowIds: ["vpn"],
          selectedIds: new Set(["vpn"]),
          onToggle: () => {},
          onToggleAll: () => {},
          getRowLabel: (id) => `Select ${id}`,
        }}
      />
    )
    const card = html.slice(
      html.indexOf("<article"),
      html.indexOf("</article>")
    )
    expect(card).toContain('aria-label="Select vpn"')
    expect(card).toContain('aria-checked="true"')
    expect(card).toContain('data-selected="true"')
    expect(card).toContain("data-[selected=true]:border-primary")
    expect(card).toContain("data-[selected=true]:bg-primary/20")
  })
})
