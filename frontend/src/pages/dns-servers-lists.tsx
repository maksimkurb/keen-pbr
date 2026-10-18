import { useTranslation } from "react-i18next"

import {
  Field,
  FieldContent,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { MultiSelectList } from "@/components/shared/multi-select-list"
import { Card, CardContent } from "@/components/ui/card"

/**
 * The main DNS servers (`dns.fallback`) and the bootstrap servers
 * (`dns.bootstrap`), shown under the table of DNS servers.
 */
export function DnsServerLists({
  bootstrap,
  bootstrapOptions,
  fallback,
  onBootstrapChange,
  onFallbackChange,
  serverTags,
}: {
  bootstrap: string[]
  /** The tags that can be bootstrap servers: not DNS-over-HTTPS ones. */
  bootstrapOptions: string[]
  fallback: string[]
  onBootstrapChange: (tags: string[]) => void
  onFallbackChange: (tags: string[]) => void
  serverTags: string[]
}) {
  const { t } = useTranslation()

  return (
    <>
      <Card>
        <CardContent className="space-y-6">
          <Field>
            <FieldLabel>{t("pages.dnsServers.fallback.title")}</FieldLabel>
            <FieldContent>
              <MultiSelectList
                addLabel={t("pages.dnsServers.fallback.add")}
                allowReorder
                emptyMessage={t("pages.dnsServers.fallback.noneAvailable")}
                onChange={onFallbackChange}
                options={serverTags}
                placeholderDescription={t(
                  "pages.dnsServers.fallback.placeholderDescription"
                )}
                placeholderTitle={t(
                  "pages.dnsServers.fallback.placeholderTitle"
                )}
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
            </FieldContent>
          </Field>
        </CardContent>
      </Card>

      <Card>
        <CardContent className="space-y-6">
          <Field>
            <FieldLabel>{t("pages.dnsServers.bootstrap.title")}</FieldLabel>
            <FieldContent>
              <MultiSelectList
                addLabel={t("pages.dnsServers.bootstrap.add")}
                allowReorder
                emptyMessage={t(
                  bootstrapOptions.length === 0
                    ? "pages.dnsServers.bootstrap.noneDefined"
                    : "pages.dnsServers.bootstrap.noneAvailable"
                )}
                onChange={onBootstrapChange}
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
              <FieldHint
                description={t("pages.dnsServers.bootstrap.description")}
              />
            </FieldContent>
          </Field>
        </CardContent>
      </Card>
    </>
  )
}
