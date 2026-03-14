import { Trash2 } from "lucide-react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { Field, FieldContent, FieldLabel } from "@/components/shared/field"
import { PageHeader } from "@/components/shared/page-header"
import { SectionCard } from "@/components/shared/section-card"

export function DnsRulesPage() {
  return (
    <div className="space-y-6">
      <PageHeader
        actions={<Button>Add DNS rule</Button>}
        description="Assign routing lists to specific DNS servers."
        title="DNS Rules"
      />

      <SectionCard
        description="Used when no DNS rule matches the current request."
        title="Fallback"
      >
        <Field>
          <FieldLabel htmlFor="dns-fallback">Fallback server tag</FieldLabel>
          <FieldContent>
            <Input defaultValue="google-dns" id="dns-fallback" />
          </FieldContent>
        </Field>
      </SectionCard>

      <DataTable
        headers={["List", "Server tag", "Match type", "Actions"]}
        rows={[
          [
            <Badge key="domains-list" variant="outline">
              my-domains
            </Badge>,
            <span className="font-medium" key="domains-server">
              vpn-dns
            </span>,
            <span className="text-sm text-muted-foreground" key="domains-type">
              domain
            </span>,
            <ActionButtons
              actions={[{ icon: <Trash2 className="h-4 w-4" />, label: "Delete" }]}
              key="domains-actions"
            />,
          ],
          [
            <Badge key="remote-list" variant="outline">
              remote-list
            </Badge>,
            <span className="font-medium" key="remote-server">
              vpn-dns
            </span>,
            <span className="text-sm text-muted-foreground" key="remote-type">
              domain suffix
            </span>,
            <ActionButtons
              actions={[{ icon: <Trash2 className="h-4 w-4" />, label: "Delete" }]}
              key="remote-actions"
            />,
          ],
        ]}
      />
    </div>
  )
}
