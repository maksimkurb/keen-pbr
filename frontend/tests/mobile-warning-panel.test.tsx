import { describe, expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"
import { MobileWarningPanel } from "../src/components/layout/mobile-warning-panel"

describe("mobile configuration panel", () => {
  test("starts as a compact dialog trigger without exposing action buttons", () => {
    const html = renderToStaticMarkup(
      <MobileWarningPanel
        mode="draft"
        title="Apply changes"
        description="Draft is ready"
      >
        <button>Apply</button>
        <button>Discard</button>
      </MobileWarningPanel>
    )
    expect(html).toContain("Apply changes")
    expect(html).toContain('aria-haspopup="dialog"')
    expect(html).toContain('aria-expanded="false"')
    expect(html).toContain("fixed inset-x-0 bottom-0")
    expect(html).toContain("safe-area-inset-bottom")
    expect(html).not.toContain(">Apply</button>")
    expect(html).not.toContain(">Discard</button>")
  })

  test("keeps operation progress and failure visible while collapsed", () => {
    const running = renderToStaticMarkup(
      <MobileWarningPanel
        mode="lifecycle-running"
        title="Applying"
        description="Progress"
      >
        Steps
      </MobileWarningPanel>
    )
    const failed = renderToStaticMarkup(
      <MobileWarningPanel
        mode="lifecycle-error"
        title="Apply failed"
        description="Retry available"
      >
        Retry
      </MobileWarningPanel>
    )
    expect(running).toContain("Applying")
    expect(running).toContain("animate-spin")
    expect(failed).toContain("Apply failed")
    expect(failed).toContain("text-warning-foreground")
    expect(failed).toContain("var(--color-warning)_15%")
    expect(failed).not.toContain("bg-destructive")
  })
})
