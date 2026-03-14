import { useState } from "react"

import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import { Input } from "@/components/ui/input"
import { FormField } from "@/components/shared/form-field"
import { PageHeader } from "@/components/shared/page-header"
import { SectionCard } from "@/components/shared/section-card"

export function GeneralConfigPage() {
  const [cron, setCron] = useState("0 */6 * * *")
  const [fwmarkStart, setFwmarkStart] = useState("0x00010000")
  const [fwmarkMask, setFwmarkMask] = useState("0xffff0000")
  const [tableStart, setTableStart] = useState("150")

  const cronError = getCronError(cron)
  const fwmarkStartError = getFwmarkStartError(fwmarkStart)
  const fwmarkMaskError = getFwmarkMaskError(fwmarkMask)
  const tableStartError = getTableStartError(tableStart)

  return (
    <>
      <PageHeader
        description="Daemon defaults, global list refresh, and advanced routing values."
        title="Settings"
      />
      <div className="space-y-4">
        <SectionCard title="General">
          <div className="flex items-center gap-3">
            <Checkbox checked />
            <span className="text-base">Global strict enforcement</span>
          </div>
          <div className="flex items-center gap-3">
            <Checkbox checked />
            <span className="text-base">Enable global lists autoupdate</span>
          </div>
          <FormField
            description={
              cronError ?? "Global schedule for refreshing remote lists."
            }
            htmlFor="general-cron"
            label="Cron"
          >
            <Input
              aria-invalid={Boolean(cronError)}
              id="general-cron"
              onChange={(event) => setCron(event.target.value)}
              value={cron}
            />
          </FormField>
        </SectionCard>

        <SectionCard title="Advanced routing settings">
          <div className="grid gap-3 md:grid-cols-3">
            <FormField
              description={
                fwmarkStartError ??
                "First fwmark assigned to outbounds. Enter as hexadecimal value."
              }
              htmlFor="fwmark-start"
              label="fwmark.start"
            >
              <Input
                aria-invalid={Boolean(fwmarkStartError)}
                id="fwmark-start"
                onChange={(event) => setFwmarkStart(event.target.value)}
                value={fwmarkStart}
              />
            </FormField>
            <FormField
              description={
                fwmarkMaskError ?? (
                  <>
                    Hex only. Must contain one consecutive run of <code>f</code>{" "}
                    digits, e.g. <code>0x00ff0000</code>.
                  </>
                )
              }
              htmlFor="fwmark-mask"
              label="fwmark.mask"
            >
              <Input
                aria-invalid={Boolean(fwmarkMaskError)}
                id="fwmark-mask"
                onChange={(event) => setFwmarkMask(event.target.value)}
                value={fwmarkMask}
              />
            </FormField>
            <FormField
              description={
                tableStartError ??
                "Base routing table number used for per-outbound policy tables."
              }
              htmlFor="table-start"
              label="iproute.table_start"
            >
              <Input
                aria-invalid={Boolean(tableStartError)}
                id="table-start"
                onChange={(event) => setTableStart(event.target.value)}
                value={tableStart}
              />
            </FormField>
          </div>
        </SectionCard>

        <div className="flex justify-end gap-2">
          <Button variant="outline">Cancel</Button>
          <Button>Save</Button>
        </div>
      </div>
    </>
  )
}

function getCronError(value: string) {
  const fields = value.trim().split(/\s+/)
  if (fields.length !== 5) {
    return "Cron must have exactly 5 fields."
  }

  const ranges: Array<[number, number]> = [
    [0, 59],
    [0, 23],
    [1, 31],
    [1, 12],
    [0, 7],
  ]

  for (const [index, field] of fields.entries()) {
    if (!isValidCronField(field, ranges[index][0], ranges[index][1])) {
      return "Enter a valid 5-field cron expression."
    }
  }

  return null
}

function isValidCronField(field: string, min: number, max: number) {
  return field.split(",").every((part) => isValidCronPart(part, min, max))
}

function isValidCronPart(part: string, min: number, max: number) {
  const stepParts = part.split("/")
  if (stepParts.length > 2) {
    return false
  }

  const [base, step] = stepParts
  if (step && !isValidNumber(step, 1, max)) {
    return false
  }

  if (base === "*") {
    return true
  }

  const rangeParts = base.split("-")
  if (rangeParts.length === 2) {
    const [start, end] = rangeParts
    return (
      isValidNumber(start, min, max) &&
      isValidNumber(end, min, max) &&
      Number(start) <= Number(end)
    )
  }

  if (rangeParts.length === 1) {
    return isValidNumber(base, min, max)
  }

  return false
}

function isValidNumber(value: string, min: number, max: number) {
  if (!/^\d+$/.test(value)) {
    return false
  }

  const numericValue = Number(value)
  return numericValue >= min && numericValue <= max
}

function getFwmarkStartError(value: string) {
  return isValidHex32(value)
    ? null
    : "fwmark.start must be a 32-bit hexadecimal value like 0x00010000."
}

function getFwmarkMaskError(value: string) {
  if (!isValidHex32(value)) {
    return "fwmark.mask must be a 32-bit hexadecimal value."
  }

  const normalizedValue = value.slice(2).toLowerCase()
  if (!/0*f+0*/.test(normalizedValue) || /[^0f]/.test(normalizedValue)) {
    return "fwmark.mask must contain one consecutive run of f digits."
  }

  return null
}

function isValidHex32(value: string) {
  return /^0x[0-9a-fA-F]{8}$/.test(value)
}

function getTableStartError(value: string) {
  if (!/^\d+$/.test(value)) {
    return "iproute.table_start must be a positive integer."
  }

  const numericValue = Number(value)
  if (!Number.isInteger(numericValue) || numericValue < 1 || numericValue > 252) {
    return "iproute.table_start must be between 1 and 252."
  }

  return null
}
