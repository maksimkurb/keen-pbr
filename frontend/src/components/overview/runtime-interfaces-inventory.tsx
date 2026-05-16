import type { RuntimeInterfaceInventoryEntry } from "@/api/generated/model/runtimeInterfaceInventoryEntry"
import { RuntimeInterfaceInventoryStatus } from "@/api/generated/model/runtimeInterfaceInventoryStatus"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { SectionCard } from "@/components/shared/section-card"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"

const MAX_ADDR_INLINE = 3

function summarizeAddresses(
  entry: RuntimeInterfaceInventoryEntry,
  formatExtraCount: (count: number) => string,
): string {
  const v4 = entry.ipv4_addresses ?? []
  const v6 = entry.ipv6_addresses ?? []
  const merged = [...v4, ...v6]
  if (merged.length === 0) {
    return "—"
  }

  const shown = merged.slice(0, MAX_ADDR_INLINE).join(", ")
  const extra = merged.length - MAX_ADDR_INLINE
  if (extra <= 0) {
    return shown
  }

  return `${shown} · ${formatExtraCount(extra)}`
}

function formatTriState(
  value: boolean | undefined,
  yes: string,
  no: string,
  unknown: string,
) {
  if (value === true) {
    return yes
  }

  if (value === false) {
    return no
  }

  return unknown
}

export function RuntimeInterfacesInventoryPanel({
  interfaces,
  errorMessage,
  isLoading,
  labels,
}: {
  interfaces: RuntimeInterfaceInventoryEntry[]
  errorMessage?: string | null
  isLoading: boolean
  labels: {
    title: string
    description: string
    empty: string
    colName: string
    colRuntimeStatus: string
    colAdminUp: string
    colOperState: string
    colCarrier: string
    colAddresses: string
    formatExtraAddresses: (count: number) => string
    yesShort: string
    noShort: string
    unknownShort: string
    statusUp: string
    statusDown: string
  }
}) {
  return (
    <div data-testid="overview-interface-inventory">
      <SectionCard description={labels.description} title={labels.title}>
        {errorMessage ? (
          <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
            <AlertDescription>{errorMessage}</AlertDescription>
          </Alert>
        ) : null}

        {isLoading ? (
          <TableSkeleton />
        ) : interfaces.length === 0 && !errorMessage ? (
          <p className="text-sm text-muted-foreground">{labels.empty}</p>
        ) : null}

        {!isLoading && !errorMessage && interfaces.length > 0 ? (
          <div className="overflow-x-auto rounded-md border">
            <Table className="min-w-[760px] text-sm">
              <TableHeader className="bg-muted/40">
                <TableRow>
                  <TableHead className="font-semibold">
                    {labels.colName}
                  </TableHead>
                  <TableHead className="font-semibold">
                    {labels.colRuntimeStatus}
                  </TableHead>
                  <TableHead className="text-center font-semibold whitespace-nowrap">
                    {labels.colAdminUp}
                  </TableHead>
                  <TableHead className="font-semibold whitespace-nowrap">
                    {labels.colOperState}
                  </TableHead>
                  <TableHead className="text-center font-semibold whitespace-nowrap">
                    {labels.colCarrier}
                  </TableHead>
                  <TableHead className="font-semibold">
                    {labels.colAddresses}
                  </TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {interfaces.map((entry) => (
                  <TableRow
                    key={entry.name}
                    data-interface-name={entry.name}
                    data-testid="overview-interface-row"
                  >
                    <TableCell className="align-top font-medium">
                      {entry.name}
                    </TableCell>
                    <TableCell className="align-top">
                      <Badge
                        size="xs"
                        variant={
                          entry.status === RuntimeInterfaceInventoryStatus.up
                            ? "success"
                            : "secondary"
                        }
                      >
                        {entry.status === RuntimeInterfaceInventoryStatus.up
                          ? labels.statusUp
                          : labels.statusDown}
                      </Badge>
                    </TableCell>
                    <TableCell className="align-top text-center tabular-nums">
                      {formatTriState(
                        entry.admin_up,
                        labels.yesShort,
                        labels.noShort,
                        labels.unknownShort,
                      )}
                    </TableCell>
                    <TableCell className="align-top font-mono text-xs">
                      {entry.oper_state?.trim()?.length
                        ? entry.oper_state
                        : labels.unknownShort}
                    </TableCell>
                    <TableCell className="align-top text-center">
                      {formatTriState(
                        entry.carrier,
                        labels.yesShort,
                        labels.noShort,
                        labels.unknownShort,
                      )}
                    </TableCell>
                    <TableCell className="max-w-[min(100vw,28rem)] align-top whitespace-normal wrap-break-word text-xs leading-snug text-muted-foreground">
                      {summarizeAddresses(entry, labels.formatExtraAddresses)}
                    </TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </div>
        ) : null}
      </SectionCard>
    </div>
  )
}
