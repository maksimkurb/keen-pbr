import { expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"
import type { RuntimeOutboundState } from "../src/api/generated/model"
import { RuntimeOutboundStatusLabel } from "../src/components/shared/runtime-outbound-state"

test("outbound dots expose status and use its semantic color", () => {
  for (const [status, color] of [
    ["healthy", "bg-success"],
    ["degraded", "bg-warning"],
    ["unavailable", "bg-destructive"],
    ["unknown", "bg-muted-foreground/40"],
  ] as const) {
    const html = renderToStaticMarkup(
      <RuntimeOutboundStatusLabel
        statusDot
        title="work"
        t={(key) => key}
        runtimeState={{ status } as RuntimeOutboundState}
      />
    )
    expect(html).toContain(color)
    expect(html).toContain(`aria-label="runtime.outboundStatus.${status}"`)
    expect(html).not.toContain('data-slot="badge"')
    expect(html).toContain("work")
  }
})

test("missing runtime state is unknown and default presentation keeps the badge", () => {
  const props = { title: "work", t: (key: string) => key }
  const dot = renderToStaticMarkup(
    <RuntimeOutboundStatusLabel {...props} statusDot />
  )
  expect(dot).toContain('aria-label="runtime.outboundStatus.unknown"')
  const badge = renderToStaticMarkup(<RuntimeOutboundStatusLabel {...props} />)
  expect(badge).toContain('data-slot="badge"')
  expect(badge).toContain("runtime.outboundStatus.unknown")
})
