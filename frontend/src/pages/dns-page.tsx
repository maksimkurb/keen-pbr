import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { FormField } from "@/components/shared/form-field"
import { PageHeader } from "@/components/shared/page-header"
import { SectionCard } from "@/components/shared/section-card"

export function DnsPage() {
  return (
    <>
      <PageHeader
        description="DNS servers, list-to-server rules, and fallback selection."
        title="DNS"
      />
      <div className="space-y-4">
        <SectionCard title="Servers">
          <DataTable
            headers={["Tag", "Address", "Detour", "Actions"]}
            rows={[
              ["vpn-dns", "10.8.0.1", "vpn", <ActionButtons labels={["Delete"]} />],
              ["google-dns", "8.8.8.8", "None", <ActionButtons labels={["Delete"]} />],
            ]}
          />
          <Button variant="outline">Add DNS server</Button>
        </SectionCard>

        <SectionCard title="Rules">
          <DataTable
            headers={["List", "Server tag", "Actions"]}
            rows={[
              ["my-domains", "vpn-dns", <ActionButtons labels={["Delete"]} />],
              ["remote-list", "vpn-dns", <ActionButtons labels={["Delete"]} />],
            ]}
          />
          <Button variant="outline">Add DNS rule</Button>
        </SectionCard>

        <SectionCard title="Fallback">
          <FormField htmlFor="dns-fallback" label="Fallback server tag">
            <Input defaultValue="google-dns" id="dns-fallback" />
          </FormField>
        </SectionCard>
      </div>
    </>
  )
}
