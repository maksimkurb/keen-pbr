import { TriangleAlert } from "lucide-react"
import type { ReactNode } from "react"
import { Trans, useTranslation } from "react-i18next"

import { FieldHint } from "@/components/shared/field"
import { MultiSelectList } from "@/components/shared/multi-select-list"
import {
  Accordion,
  AccordionItem,
  AccordionPanel,
  AccordionTrigger,
} from "@/components/ui/accordion"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Card } from "@/components/ui/card"
import type { DnsServerRole } from "@/pages/dns-servers-utils"

/** The main DNS servers (`dns.fallback`). */
export function FallbackServerList({
  fallback,
  onChange,
  serverTags,
}: {
  fallback: string[]
  onChange: (tags: string[]) => void
  serverTags: string[]
}) {
  const { t } = useTranslation()

  return (
    <div className="space-y-4">
      {fallback.length === 0 ? (
        <Alert variant="warning">
          <TriangleAlert />
          <AlertDescription>
            {t("pages.dnsServers.fallback.warning")}
          </AlertDescription>
        </Alert>
      ) : null}
      <div className="space-y-2">
        <MultiSelectList
          addLabel={t("pages.dnsServers.fallback.add")}
          allowReorder
          emptyMessage={t("pages.dnsServers.fallback.noneAvailable")}
          onChange={onChange}
          options={serverTags}
          placeholderDescription=""
          placeholderTitle={t("pages.dnsServers.fallback.placeholderTitle")}
          value={fallback}
        />
        <FieldHint
          description={
            serverTags.length === 0 ? (
              <>
                {t("pages.dnsServers.fallback.description")}{" "}
                {t("pages.dnsServers.fallback.noneDefined")}
              </>
            ) : (
              t("pages.dnsServers.fallback.description")
            )
          }
        />
      </div>
    </div>
  )
}

/** The bootstrap servers (`dns.bootstrap`). */
export function BootstrapServerList({
  bootstrap,
  bootstrapOptions,
  dohWithoutIps,
  onChange,
}: {
  bootstrap: string[]
  /** The tags that can be bootstrap servers: not DNS-over-HTTPS ones. */
  bootstrapOptions: string[]
  /** DoH servers to look up while no bootstrap server is chosen. */
  dohWithoutIps: string[]
  onChange: (tags: string[]) => void
}) {
  const { t } = useTranslation()

  return (
    <div className="space-y-4">
      {dohWithoutIps.length > 0 ? (
        <Alert variant="warning">
          <TriangleAlert />
          <AlertDescription>
            <Trans
              components={{ bold: <strong /> }}
              i18nKey="pages.dnsServers.bootstrap.warning"
              values={{ tags: dohWithoutIps.join(", ") }}
            />
          </AlertDescription>
        </Alert>
      ) : null}
      <div className="space-y-2">
        <MultiSelectList
          addLabel={t("pages.dnsServers.bootstrap.add")}
          allowReorder
          emptyMessage={t(
            bootstrapOptions.length === 0
              ? "pages.dnsServers.bootstrap.noneDefined"
              : "pages.dnsServers.bootstrap.noneAvailable"
          )}
          onChange={onChange}
          options={bootstrapOptions}
          placeholderDescription={
            bootstrapOptions.length === 0
              ? t("pages.dnsServers.bootstrap.noPlainHint")
              : ""
          }
          placeholderTitle={
            bootstrapOptions.length === 0
              ? t("pages.dnsServers.bootstrap.systemTitle")
              : t("pages.dnsServers.bootstrap.autoTitle", {
                  tag: bootstrapOptions[0],
                })
          }
          value={bootstrap}
        />
        <FieldHint description={t("pages.dnsServers.bootstrap.description")} />
      </div>
    </div>
  )
}

/**
 * The main and the bootstrap servers as collapsed sections; a collapsed
 * section says which servers are in use.
 */
export function DnsServerListsAccordion({
  bootstrap,
  bootstrapOptions,
  dohWithoutIps,
  fallback,
  fallbackWarning,
  onBootstrapChange,
  onFallbackChange,
  serverTags,
}: {
  bootstrap: string[]
  bootstrapOptions: string[]
  dohWithoutIps: string[]
  fallback: string[]
  fallbackWarning: boolean
  onBootstrapChange: (tags: string[]) => void
  onFallbackChange: (tags: string[]) => void
  serverTags: string[]
}) {
  const { t } = useTranslation()
  const fallbackSummary =
    fallback.length > 0
      ? fallback.join(", ")
      : t("pages.dnsServers.fallback.noneSummary")
  const bootstrapSummary =
    bootstrap.length > 0
      ? bootstrap.join(", ")
      : t("pages.dnsServers.bootstrap.noneSummary")

  return (
    <Card className="gap-0 py-0">
      <Accordion multiple>
        <ListSection
          summary={fallbackSummary}
          title={t("pages.dnsServers.fallback.title")}
          value="fallback"
          warning={fallbackWarning}
        >
          <FallbackServerList
            fallback={fallback}
            onChange={onFallbackChange}
            serverTags={serverTags}
          />
        </ListSection>
        <ListSection
          summary={bootstrapSummary}
          title={t("pages.dnsServers.bootstrap.title")}
          value="bootstrap"
          warning={dohWithoutIps.length > 0}
        >
          <BootstrapServerList
            bootstrap={bootstrap}
            bootstrapOptions={bootstrapOptions}
            dohWithoutIps={dohWithoutIps}
            onChange={onBootstrapChange}
          />
        </ListSection>
      </Accordion>
    </Card>
  )
}

function ListSection({
  children,
  summary,
  title,
  value,
  warning,
}: {
  children: ReactNode
  summary: string
  title: string
  value: string
  warning: boolean
}) {
  const { t } = useTranslation()

  return (
    <AccordionItem className="border-b last:border-b-0" value={value}>
      <AccordionTrigger className="px-4 py-3">
        <span className="flex min-w-0 flex-1 flex-col gap-0.5 sm:flex-row sm:items-center sm:gap-3">
          <span className="flex items-center gap-1.5 font-medium">
            <span className="group-hover:underline">{title}</span>
            {warning ? (
              <TriangleAlert
                aria-label={t("pages.dnsServers.needsAttention")}
                className="size-4 text-orange-500"
                role="img"
              />
            ) : null}
          </span>
          <span className="truncate text-sm text-muted-foreground group-data-[panel-open]:hidden">
            {summary}
          </span>
        </span>
      </AccordionTrigger>
      <AccordionPanel className="px-4 pb-4">{children}</AccordionPanel>
    </AccordionItem>
  )
}

/** The badges under a server name: main server, bootstrap server. */
export function DnsServerRoleBadges({ roles }: { roles: DnsServerRole[] }) {
  const { t } = useTranslation()
  if (roles.length === 0) {
    return null
  }

  return (
    <div className="mt-1 flex flex-wrap gap-1">
      {roles.map((role) => (
        <Badge key={role.kind} size="xs" variant="secondary">
          {t(
            role.auto
              ? "pages.dnsServers.roles.bootstrapAuto"
              : role.position
                ? `pages.dnsServers.roles.${role.kind}Numbered`
                : `pages.dnsServers.roles.${role.kind}`,
            { position: role.position }
          )}
        </Badge>
      ))}
    </div>
  )
}
