import { Trash2 } from "lucide-react"
import { useState } from "react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { Field, FieldContent, FieldLabel } from "@/components/shared/field"
import { PageHeader } from "@/components/shared/page-header"
import { SectionCard } from "@/components/shared/section-card"

const dnsServerTags = ["google-dns", "vpn-dns", "local-dns"] as const

export function DnsRulesPage() {
  const [fallbackServerTag, setFallbackServerTag] = useState<
    (typeof dnsServerTags)[number] | undefined
  >("google-dns")

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
          <FieldLabel>Fallback server tag</FieldLabel>
          <FieldContent>
            <Select
              onValueChange={(value) =>
                setFallbackServerTag(value as (typeof dnsServerTags)[number])
              }
              value={fallbackServerTag}
            >
              <SelectTrigger>
                <SelectValue placeholder="Select a DNS server" />
              </SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  <SelectLabel>DNS servers</SelectLabel>
                  {dnsServerTags.map((serverTag) => (
                    <SelectItem key={serverTag} value={serverTag}>
                      {serverTag}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
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
              actions={[
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
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
              actions={[
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="remote-actions"
            />,
          ],
        ]}
      />
    </div>
  )
}
