import { Trash2 } from "lucide-react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { Field, FieldContent, FieldLabel } from "@/components/shared/field"
import { PageHeader } from "@/components/shared/page-header"
import { SectionCard } from "@/components/shared/section-card"

export function DnsServersPage() {
  return (
    <div className="space-y-6">
      <PageHeader
        description="Configure upstream DNS servers and default fallback behavior."
        title="DNS Servers"
      />

      <SectionCard action={<Button variant="outline">Add DNS server</Button>} title="Servers">
        <DataTable
          headers={["Tag", "Address", "Detour", "Actions"]}
          rows={[
            [
              <div className="font-medium" key="vpn-dns-tag">
                vpn-dns
              </div>,
              <span className="text-sm text-muted-foreground" key="vpn-dns-address">
                10.8.0.1
              </span>,
              <Badge key="vpn-dns-detour" variant="outline">
                vpn
              </Badge>,
              <ActionButtons
                actions={[{ icon: <Trash2 className="h-4 w-4" />, label: "Delete" }]}
                key="vpn-dns-actions"
              />,
            ],
            [
              <div className="font-medium" key="google-dns-tag">
                google-dns
              </div>,
              <span className="text-sm text-muted-foreground" key="google-dns-address">
                8.8.8.8
              </span>,
              <Badge key="google-dns-detour" variant="secondary">
                none
              </Badge>,
              <ActionButtons
                actions={[{ icon: <Trash2 className="h-4 w-4" />, label: "Delete" }]}
                key="google-dns-actions"
              />,
            ],
          ]}
        />
      </SectionCard>

      <SectionCard title="Fallback">
        <Field>
          <FieldLabel htmlFor="dns-fallback">Fallback server tag</FieldLabel>
          <FieldContent>
            <Input defaultValue="google-dns" id="dns-fallback" />
          </FieldContent>
        </Field>
      </SectionCard>
    </div>
  )
}
