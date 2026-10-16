import { expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"
import { DiagnosticsPanel } from "../src/components/overview/diagnostics-panel"
import type { OverviewIssue } from "../src/components/overview/overview-issues"

function render(issues: OverviewIssue[]) {
  return renderToStaticMarkup(
    <DiagnosticsPanel
      issues={issues}
      healthy={[]}
      isLoading={false}
      showHealthy={false}
      onShowHealthyChange={() => {}}
      onDownload={() => {}}
      highlighted={false}
    />
  )
}

test("healthy diagnostics use an outline download button and a desktop-only checkbox", () => {
  const html = render([])
  expect(html).toContain("md:flex")
  expect(html).toContain("border-border bg-background")
  expect(html).not.toContain("animate-[diagnostics-ping")
})

test("warnings highlight download in yellow; errors take precedence in red", () => {
  const warning = { key: "warn", tone: "warn", title: "Warning" } as const
  const error = { key: "error", tone: "bad", title: "Error" } as const
  expect(render([warning])).toContain("bg-warning/15")
  expect(render([warning])).toContain("motion-safe:animate-[diagnostics-ping")
  const mixed = render([warning, error])
  expect(mixed).toContain("bg-destructive/10")
  expect(mixed).not.toContain("bg-warning/15")
})
