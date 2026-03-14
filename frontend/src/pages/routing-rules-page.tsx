import { ArrowDown, ArrowUp, Pencil, Trash2 } from "lucide-react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { PageHeader } from "@/components/shared/page-header"

export function RoutingRulesPage() {
  return (
    <div className="space-y-6">
      <PageHeader
        actions={<Button>New rule</Button>}
        description="Rule list with ordering controls and constrained select fields."
        title="Routing rules"
      />

      <DataTable
        headers={["Order", "List", "Outbound", "Proto", "Dest addr", "Dest port", "Actions"]}
        rows={[
          [
            "1",
            <Badge key="rule1-list" variant="outline">
              my-domains
            </Badge>,
            <Badge key="rule1-outbound">vpn</Badge>,
            "tcp/udp",
            <span className="text-sm text-muted-foreground" key="rule1-dest">
              -
            </span>,
            <span className="text-sm text-muted-foreground" key="rule1-port">
              -
            </span>,
            <ActionButtons
              actions={[
                { icon: <ArrowUp className="h-4 w-4" />, label: "Move up" },
                { icon: <ArrowDown className="h-4 w-4" />, label: "Move down" },
                { icon: <Pencil className="h-4 w-4" />, label: "Edit" },
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="rule1-actions"
            />,
          ],
          [
            "2",
            <Badge key="rule2-list" variant="outline">
              remote-list
            </Badge>,
            <Badge key="rule2-outbound">auto-select</Badge>,
            "tcp",
            <span className="text-sm text-muted-foreground" key="rule2-dest">
              -
            </span>,
            "443",
            <ActionButtons
              actions={[
                { icon: <ArrowUp className="h-4 w-4" />, label: "Move up" },
                { icon: <ArrowDown className="h-4 w-4" />, label: "Move down" },
                { icon: <Pencil className="h-4 w-4" />, label: "Edit" },
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="rule2-actions"
            />,
          ],
          [
            "3",
            <Badge key="rule3-list" variant="outline">
              my-ips
            </Badge>,
            <Badge key="rule3-outbound" variant="secondary">
              wan
            </Badge>,
            "any",
            "198.51.100.0/24",
            <span className="text-sm text-muted-foreground" key="rule3-port">
              -
            </span>,
            <ActionButtons
              actions={[
                { icon: <ArrowUp className="h-4 w-4" />, label: "Move up" },
                { icon: <ArrowDown className="h-4 w-4" />, label: "Move down" },
                { icon: <Pencil className="h-4 w-4" />, label: "Edit" },
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="rule3-actions"
            />,
          ],
        ]}
      />
    </div>
  )
}
