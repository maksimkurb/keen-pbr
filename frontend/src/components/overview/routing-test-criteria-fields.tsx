import { ChevronDown } from "lucide-react"
import { useTranslation } from "react-i18next"
import { Button } from "@/components/ui/button"
import {
  InputGroup,
  InputGroupAddon,
  InputGroupInput,
} from "@/components/ui/input-group"
import {
  Collapsible,
  CollapsibleContent,
  CollapsibleTrigger,
} from "@/components/ui/collapsible"
import {
  Select,
  SelectTrigger,
  SelectValue,
  SelectContent,
  SelectItem,
} from "@/components/ui/select"
import type { RoutingTestCriteria } from "./routing-test-criteria"

export function RoutingTestCriteriaFields({
  value,
  onChange,
}: {
  value: RoutingTestCriteria
  onChange: (value: RoutingTestCriteria) => void
}) {
  const { t } = useTranslation()
  const set = (key: keyof RoutingTestCriteria, next: string) =>
    onChange({ ...value, [key]: next })
  const protocols = [
    { value: "tcp", label: "TCP" },
    { value: "udp", label: "UDP" },
    { value: "other", label: t("overview.routingTest.otherProtocol") },
  ]
  return (
    <div className="space-y-2">
      <div className="grid grid-cols-1 gap-3 sm:grid-cols-2">
        <InputGroup className="h-11 sm:h-9">
          <InputGroupAddon>
            <label htmlFor="routing-test-protocol">
              {t("overview.routingTest.protocol")}
            </label>
          </InputGroupAddon>
          <Select
            value={value.proto}
            items={protocols}
            onValueChange={(next) => {
              if (next) set("proto", next)
            }}
          >
            <SelectTrigger
              id="routing-test-protocol"
              aria-label={t("overview.routingTest.protocol")}
              className="h-full flex-1 rounded-none border-0 bg-transparent shadow-none focus-visible:ring-0 data-[size=default]:h-full"
            >
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {protocols.map((item) => (
                <SelectItem key={item.value} value={item.value}>
                  {item.label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </InputGroup>
        <InputGroup className="h-11 sm:h-9">
          <InputGroupAddon>
            <label htmlFor="routing-test-port">
              {t("overview.routingTest.port")}
            </label>
          </InputGroupAddon>
          <InputGroupInput
            id="routing-test-port"
            type="number"
            inputMode="numeric"
            min={1}
            max={65535}
            step={1}
            required={value.proto !== "other"}
            disabled={value.proto === "other"}
            value={value.dest_port}
            onChange={(event) => set("dest_port", event.target.value)}
          />
        </InputGroup>
      </div>
      <Collapsible>
        <CollapsibleTrigger
          render={
            <Button type="button" variant="link" className="h-7 px-0 py-0" />
          }
          className="group"
        >
          {t("overview.routingTest.otherCriteria")}
          <ChevronDown className="transition-transform group-data-panel-open:rotate-180" />
        </CollapsibleTrigger>
        <CollapsibleContent>
          <div className="grid gap-3 pt-1 pb-3 sm:grid-cols-2">
            <InputGroup className="h-11 sm:col-span-2 sm:h-9">
              <InputGroupAddon>
                <label htmlFor="routing-test-source-ip">
                  {t("overview.routingDiagnostics.conditions.sourceIp")}
                </label>
              </InputGroupAddon>
              <InputGroupInput
                id="routing-test-source-ip"
                placeholder={t("overview.routingTest.sourceIpPlaceholder")}
                value={value.src_addr}
                onChange={(event) => set("src_addr", event.target.value)}
              />
            </InputGroup>
            <InputGroup className="h-11 sm:h-9">
              <InputGroupAddon>
                <label htmlFor="routing-test-source-port">
                  {t("pages.routingRuleUpsert.fields.sourcePort")}
                </label>
              </InputGroupAddon>
              <InputGroupInput
                id="routing-test-source-port"
                type="number"
                inputMode="numeric"
                min={1}
                max={65535}
                step={1}
                disabled={value.proto === "other"}
                value={value.src_port}
                onChange={(event) => set("src_port", event.target.value)}
              />
            </InputGroup>
            <InputGroup className="h-11 sm:h-9">
              <InputGroupAddon>
                <label htmlFor="routing-test-dscp">
                  {t("pages.routingRuleUpsert.fields.dscp")}
                </label>
              </InputGroupAddon>
              <InputGroupInput
                id="routing-test-dscp"
                type="number"
                inputMode="numeric"
                min={0}
                max={63}
                step={1}
                placeholder="0–63"
                value={value.dscp}
                onChange={(event) => set("dscp", event.target.value)}
              />
            </InputGroup>
          </div>
        </CollapsibleContent>
      </Collapsible>
    </div>
  )
}
