import { ExternalLink, Pencil, RefreshCw, Trash2 } from "lucide-react"
import { useLocation } from "wouter"

import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { PageHeader } from "@/components/shared/page-header"
import { StatsDisplay } from "@/components/shared/stats-display"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { getListSourceLabel, listItems } from "@/pages/lists-shared"

export function ListsPage() {
  const [, navigate] = useLocation()

  return (
    <div className="space-y-6">
      <PageHeader
        actions={<Button onClick={() => navigate("/lists/create")}>New list</Button>}
        description="Manage domain and IP lists used by routing rules."
        title="Lists"
      />

      <DataTable
        headers={["Name", "Type", "Stats", "Rules", "Actions"]}
        rows={listItems.map((list) => [
          <div className="space-y-1" key={`${list.id}-name`}>
            <div className="flex items-center gap-2 font-medium">
              {list.draft.name}
              {list.locationIcon === "external" ? (
                <ExternalLink className="h-3 w-3 text-muted-foreground" />
              ) : null}
            </div>
            <div className="text-sm text-muted-foreground md:text-xs">{list.locationLabel}</div>
          </div>,
          <Badge key={`${list.id}-type`} variant={list.typeVariant}>
            {getListSourceLabel(list.draft)}
          </Badge>,
          <StatsDisplay
            ipv4Subnets={list.stats.ipv4Subnets}
            ipv6Subnets={list.stats.ipv6Subnets}
            key={`${list.id}-stats`}
            totalHosts={list.stats.totalHosts}
          />,
          <Badge key={`${list.id}-rule`} variant="outline">
            {list.rule}
          </Badge>,
          <ActionButtons
            actions={[
              ...(list.canRefresh
                ? [{ icon: <RefreshCw className="h-4 w-4" />, label: "Update" }]
                : []),
              {
                icon: <Pencil className="h-4 w-4" />,
                label: "Edit",
                onClick: () => navigate(`/lists/${list.id}/edit`),
              },
              { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
            ]}
            key={`${list.id}-actions`}
          />,
        ])}
      />
    </div>
  )
}
