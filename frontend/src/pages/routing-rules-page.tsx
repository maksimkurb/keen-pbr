import { Button } from "@/components/ui/button"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { PageHeader } from "@/components/shared/page-header"
import { SectionCard } from "@/components/shared/section-card"

export function RoutingRulesPage() {
  return (
    <>
      <PageHeader
        actions={<Button>New rule</Button>}
        description="Rule list with ordering controls and constrained select fields."
        title="Routing rules"
      />
      <SectionCard title="Rules table">
        <DataTable
          headers={["Order", "List", "Outbound", "Proto", "Dest addr", "Dest port", "Actions"]}
          rows={[
            ["1", "my-domains", "vpn", "tcp/udp", "", "", <ActionButtons labels={["↑", "↓", "Edit", "Delete"]} />],
            ["2", "remote-list", "auto-select", "tcp", "", "443", <ActionButtons labels={["↑", "↓", "Edit", "Delete"]} />],
            ["3", "my-ips", "wan", "any", "198.51.100.0/24", "", <ActionButtons labels={["↑", "↓", "Edit", "Delete"]} />],
          ]}
        />
      </SectionCard>
    </>
  )
}
