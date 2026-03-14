import { Trash2 } from "lucide-react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { PageHeader } from "@/components/shared/page-header"
import { SectionCard } from "@/components/shared/section-card"

export function DnsRulesPage() {
  return (
    <div className="space-y-6">
      <PageHeader
        description="Assign routing lists to specific DNS servers."
        title="DNS Rules"
      />

      <SectionCard action={<Button variant="outline">Add DNS rule</Button>} title="Rules">
        <DataTable
          headers={["List", "Server tag", "Actions"]}
          rows={[
            [
              <Badge key="domains-list" variant="outline">
                my-domains
              </Badge>,
              <span className="font-medium" key="domains-server">
                vpn-dns
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
              <ActionButtons
                actions={[{ icon: <Trash2 className="h-4 w-4" />, label: "Delete" }]}
                key="remote-actions"
              />,
            ],
          ]}
        />
      </SectionCard>
    </div>
  )
}
