import { expect, test } from "bun:test"
import { renderToStaticMarkup } from "react-dom/server"

import {
  Accordion,
  AccordionItem,
  AccordionPanel,
  AccordionTrigger,
} from "../src/components/ui/accordion"

test("accordion starts collapsed and exposes an accessible trigger", () => {
  const html = renderToStaticMarkup(
    <Accordion>
      <AccordionItem>
        <AccordionTrigger>Rule diagnostics</AccordionTrigger>
        <AccordionPanel>Rule details</AccordionPanel>
      </AccordionItem>
    </Accordion>
  )

  expect(html).toContain('aria-expanded="false"')
  expect(html).toContain("Rule diagnostics")
  expect(html).not.toContain("Rule details")
})
