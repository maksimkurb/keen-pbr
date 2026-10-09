import { Toolbox } from "lucide-react"
import { useState, type ComponentProps, type ReactNode } from "react"
import { useTranslation } from "react-i18next"

import { useQueryClient } from "@tanstack/react-query"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { ResolverIntegrationMode } from "@/api/generated/model/resolverIntegrationMode"
import { effectiveResolverIntegration, selectConfig } from "@/api/selectors"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import {
  useGetConfig,
  useGetHealthRouting,
  useGetRuntimeInterfaces,
} from "@/api/queries"
import {
  Field,
  FieldContent,
  FieldDescription,
  FieldGroup,
  FieldHint,
  FieldLabel,
  FieldSeparator,
} from "@/components/shared/field"
import { InterfaceMultiSelectList } from "@/components/shared/interface-picker"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { ServerValidationAlert } from "@/components/shared/server-validation-alert"
import { Button } from "@/components/ui/button"
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { Checkbox } from "@/components/ui/checkbox"
import { Input } from "@/components/ui/input"
import { RadioGroup, RadioGroupItem } from "@/components/ui/radio-group"
import { Skeleton } from "@/components/ui/skeleton"
import { Switch } from "@/components/ui/switch"
import { type FieldBinding, bindInput, useDraftForm } from "@/lib/draft-form"
import { toast } from "sonner"

/**
 * Draft of the settings page. Names and nesting mirror the config document,
 * so a draft path is exactly the API path of that value (`daemon.ipv6_enabled`).
 */
export type SettingsDraft = {
  device_name: string
  daemon: {
    strict_enforcement: boolean
    skip_marked_packets: boolean
    clear_dynamic_sets_on_apply: boolean
    ipv6_enabled: boolean
    ipset_hashsize: string
    ipset_maxelem: string
  }
  route: {
    inbound_interfaces: string[]
  }
  lists_autoupdate: {
    enabled: boolean
    cron: string
  }
  fwmark: {
    start: string
    mask: string
  }
  iproute: {
    table_start: string
    process_router_traffic: boolean
  }
  dns: {
    resolver_integration: ResolverIntegrationMode
  }
  intercept: {
    enabled: boolean
    min_ttl_s: string
    max_ttl_s: string
    dns: {
      enabled: boolean
      queue_num: string
      hold_timeout_ms: string
      marker: {
        domain: string
        answer_ipv4: string
      }
    }
    l7: {
      enabled: boolean
      nflog_group: string
      tls: boolean
      http: boolean
      quic: boolean
    }
  }
}

const fallbackDraft: SettingsDraft = {
  device_name: "",
  daemon: {
    strict_enforcement: true,
    skip_marked_packets: true,
    clear_dynamic_sets_on_apply: false,
    ipv6_enabled: true,
    ipset_hashsize: "",
    ipset_maxelem: "",
  },
  route: { inbound_interfaces: [] },
  lists_autoupdate: { enabled: false, cron: "0 4 * * 0" },
  fwmark: { start: "0x00010000", mask: "0xffff0000" },
  iproute: { table_start: "150", process_router_traffic: false },
  dns: { resolver_integration: "none" },
  intercept: {
    enabled: true,
    min_ttl_s: "300",
    max_ttl_s: "86400",
    dns: {
      enabled: true,
      queue_num: "9053",
      hold_timeout_ms: "30",
      marker: { domain: "check.keen.pbr", answer_ipv4: "127.0.0.88" },
    },
    l7: {
      enabled: true,
      nflog_group: "9054",
      tls: true,
      http: true,
      quic: true,
    },
  },
}

export function GeneralConfigPage() {
  const { t } = useTranslation()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)

  return (
    <div className="space-y-6">
      <PageHeader
        description={t("pages.settings.description")}
        title={t("pages.settings.title")}
      />

      {configQuery.isLoading ? (
        <GeneralConfigPageSkeleton />
      ) : configQuery.isError || !loadedConfig ? (
        <ListPlaceholder
          description={t("common.loadErrorDescription")}
          title={t("common.unableToLoadData")}
          variant="error"
        />
      ) : (
        <LoadedGeneralConfigPage loadedConfig={loadedConfig} />
      )}
    </div>
  )
}

type LoadedGeneralConfigPageProps = {
  loadedConfig: ConfigObject
}

function LoadedGeneralConfigPage({
  loadedConfig,
}: LoadedGeneralConfigPageProps) {
  const { t } = useTranslation()
  const [advancedMode, setAdvancedMode] = useState(() => {
    try {
      return localStorage.getItem("keen-pbr-settings-mode") === "advanced"
    } catch {
      return false
    }
  })
  const queryClient = useQueryClient()
  const runtimeInterfacesQuery = useGetRuntimeInterfaces()
  const routingHealthQuery = useGetHealthRouting()
  const usesNftables =
    routingHealthQuery.data?.status === 200 &&
    routingHealthQuery.data.data.firewall_backend === "nftables"

  const postConfigMutation = usePostConfigMutation()

  const form = useDraftForm<SettingsDraft>(getDraftFromConfig(loadedConfig))

  const save = async (value: SettingsDraft) => {
    const updatedConfig = buildUpdatedConfig(loadedConfig, value)

    try {
      await postConfigMutation.mutateAsync({ data: updatedConfig })
    } catch (error) {
      const message = form.setApiError(error as ApiError)
      if (message) {
        toast.error(message, { richColors: true })
      }
      return
    }

    toast.success(t("pages.settings.saved"))

    await Promise.all([
      queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
      queryClient.invalidateQueries({ queryKey: queryKeys.healthService() }),
      queryClient.invalidateQueries({ queryKey: queryKeys.healthRouting() }),
    ])

    form.reset(getDraftFromConfig(updatedConfig))
  }

  const isPending = form.isSubmitting
  const runtimeInterfaces =
    runtimeInterfacesQuery.data?.status === 200
      ? runtimeInterfacesQuery.data.data.interfaces
      : []

  const handleCancel = () => {
    form.reset(getDraftFromConfig(loadedConfig))
  }

  const cron = form.field("lists_autoupdate.cron")
  const inboundInterfacesError = form.errorFor("route.inbound_interfaces")

  return (
    <>
      <Card>
        <CardHeader>
          <CardTitle>{t("pages.settings.webUi.title")}</CardTitle>
          <CardDescription>
            {t("pages.settings.webUi.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <Field>
            <FieldLabel htmlFor="device-name">
              {t("pages.settings.general.deviceNameLabel")}
            </FieldLabel>
            <Input
              id="device-name"
              maxLength={128}
              placeholder={t("pages.settings.general.deviceNamePlaceholder")}
              {...bindInput(form.field("device_name"))}
            />
            <FieldDescription>
              {t("pages.settings.general.deviceNameHint")}
            </FieldDescription>
          </Field>
          <Field className="mt-6">
            <FieldLabel id="settings-mode-label">
              {t("pages.settings.webUi.settingsModeLabel")}
            </FieldLabel>
            <RadioGroup
              aria-labelledby="settings-mode-label"
              className="gap-3"
              value={advancedMode ? "advanced" : "simple"}
              onValueChange={(value) => {
                setAdvancedMode(value === "advanced")
                try {
                  localStorage.setItem("keen-pbr-settings-mode", value)
                } catch {
                  // Storage may be unavailable; the mode still works for this page.
                }
              }}
            >
              <FieldLabel htmlFor="settings-mode-simple">
                <RadioGroupItem id="settings-mode-simple" value="simple" />
                {t("pages.settings.webUi.simpleMode")}
              </FieldLabel>
              <FieldLabel
                htmlFor="settings-mode-advanced"
                className="text-amber-700 dark:text-amber-400"
              >
                <RadioGroupItem id="settings-mode-advanced" value="advanced" />
                {t("pages.settings.webUi.advancedMode")}
                <Toolbox aria-hidden="true" className="size-4 shrink-0" />
              </FieldLabel>
            </RadioGroup>
            <FieldDescription>
              {t("pages.settings.webUi.settingsModeHint")}
            </FieldDescription>
          </Field>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>{t("pages.settings.intercept.title")}</CardTitle>
          <CardDescription>
            {t("pages.settings.intercept.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <BooleanSettingField
              field={form.field("intercept.enabled")}
              id="intercept-enabled"
              label={t("pages.settings.intercept.enabledLabel")}
              hint={t("pages.settings.intercept.enabledHint")}
            />

            <FieldSeparator />

            <div className="grid gap-6 md:grid-cols-2">
              <NumberSettingField
                field={form.field("intercept.min_ttl_s")}
                id="intercept-min-ttl"
                label={t("pages.settings.intercept.minTtlLabel")}
                hint={t("pages.settings.intercept.minTtlHint")}
              />
              <NumberSettingField
                field={form.field("intercept.max_ttl_s")}
                id="intercept-max-ttl"
                label={t("pages.settings.intercept.maxTtlLabel")}
                hint={t("pages.settings.intercept.maxTtlHint")}
              />
            </div>

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("intercept.dns.enabled")}
              id="intercept-dns-enabled"
              label={t("pages.settings.intercept.dnsEnabledLabel")}
              hint={t("pages.settings.intercept.dnsEnabledHint")}
            />

            <div className="grid gap-6 md:grid-cols-2">
              <NumberSettingField
                field={form.field("intercept.dns.hold_timeout_ms")}
                id="intercept-dns-timeout"
                label={t("pages.settings.intercept.holdTimeoutLabel")}
                hint={t("pages.settings.intercept.holdTimeoutHint")}
              />
              {advancedMode && (
                <div className="[&_[data-slot=field-description]]:text-amber-700 dark:[&_[data-slot=field-description]]:text-amber-400 [&_[data-slot=field-label]]:text-amber-700 dark:[&_[data-slot=field-label]]:text-amber-400">
                  <NumberSettingField
                    field={form.field("intercept.dns.queue_num")}
                    id="intercept-dns-queue"
                    label={
                      <span className="inline-flex items-center gap-2">
                        {t("pages.settings.intercept.queueLabel")}
                        <Toolbox
                          aria-hidden="true"
                          className="size-4 shrink-0"
                        />
                      </span>
                    }
                    hint={t("pages.settings.intercept.queueHint")}
                  />
                </div>
              )}
            </div>

            {advancedMode && (
              <div className="grid gap-6 md:grid-cols-2 [&_[data-slot=field-description]]:text-amber-700 dark:[&_[data-slot=field-description]]:text-amber-400 [&_[data-slot=field-label]]:text-amber-700 dark:[&_[data-slot=field-label]]:text-amber-400">
                <TextSettingField
                  field={form.field("intercept.dns.marker.domain")}
                  id="intercept-marker-domain"
                  label={
                    <span className="inline-flex items-center gap-2">
                      {t("pages.settings.intercept.markerDomainLabel")}
                      <Toolbox aria-hidden="true" className="size-4 shrink-0" />
                    </span>
                  }
                  hint={t("pages.settings.intercept.markerDomainHint")}
                />
                <TextSettingField
                  field={form.field("intercept.dns.marker.answer_ipv4")}
                  id="intercept-marker-address"
                  label={
                    <span className="inline-flex items-center gap-2">
                      {t("pages.settings.intercept.markerAddressLabel")}
                      <Toolbox aria-hidden="true" className="size-4 shrink-0" />
                    </span>
                  }
                  hint={t("pages.settings.intercept.markerAddressHint")}
                />
              </div>
            )}

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("intercept.l7.enabled")}
              id="intercept-l7-enabled"
              label={t("pages.settings.intercept.l7EnabledLabel")}
              hint={t("pages.settings.intercept.l7EnabledHint")}
            />

            {advancedMode && (
              <div className="[&_[data-slot=field-description]]:text-amber-700 dark:[&_[data-slot=field-description]]:text-amber-400 [&_[data-slot=field-label]]:text-amber-700 dark:[&_[data-slot=field-label]]:text-amber-400">
                <NumberSettingField
                  field={form.field("intercept.l7.nflog_group")}
                  id="intercept-l7-group"
                  label={
                    <span className="inline-flex items-center gap-2">
                      {t("pages.settings.intercept.nflogGroupLabel")}
                      <Toolbox aria-hidden="true" className="size-4 shrink-0" />
                    </span>
                  }
                  hint={t("pages.settings.intercept.nflogGroupHint")}
                />
              </div>
            )}

            <div className="grid gap-6 md:grid-cols-3">
              <BooleanSettingField
                field={form.field("intercept.l7.tls")}
                id="intercept-l7-tls"
                label={t("pages.settings.intercept.tlsLabel")}
              />
              <BooleanSettingField
                field={form.field("intercept.l7.http")}
                id="intercept-l7-http"
                label={t("pages.settings.intercept.httpLabel")}
              />
              <BooleanSettingField
                field={form.field("intercept.l7.quic")}
                id="intercept-l7-quic"
                label={t("pages.settings.intercept.quicLabel")}
              />
            </div>
          </FieldGroup>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>{t("pages.settings.general.title")}</CardTitle>
          <CardDescription>
            {t("pages.settings.general.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <Field invalid={Boolean(inboundInterfacesError)}>
              <FieldLabel htmlFor="inbound-interfaces">
                {t("pages.settings.general.inboundInterfacesLabel")}
              </FieldLabel>
              <FieldContent>
                <div id="inbound-interfaces">
                  <InterfaceMultiSelectList
                    name={"route.inbound_interfaces"}
                    interfaces={runtimeInterfaces}
                    value={form.values.route.inbound_interfaces}
                    onChange={(value) =>
                      form.setValue("route.inbound_interfaces", value)
                    }
                    addLabel={t(
                      "pages.settings.general.inboundInterfacesAddAction"
                    )}
                    emptyMessage={t(
                      "pages.settings.general.inboundInterfacesNoAvailable"
                    )}
                    placeholderTitle={t(
                      "pages.settings.general.inboundInterfacesEmptyTitle"
                    )}
                    placeholderDescription={t(
                      "pages.settings.general.inboundInterfacesEmptyDescription"
                    )}
                    error={inboundInterfacesError}
                  />
                </div>
                <FieldDescription>
                  {t("pages.settings.general.inboundInterfacesHint")}
                </FieldDescription>
              </FieldContent>
            </Field>
            <FieldSeparator />

            <BooleanSettingField
              field={form.field("daemon.strict_enforcement")}
              hint={t("pages.settings.general.strictEnforcementHint")}
              id="strict-enforcement"
              label={t("pages.settings.general.strictEnforcementLabel")}
            />

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("daemon.skip_marked_packets")}
              hint={t("pages.settings.general.skipMarkedPacketsHint")}
              id="skip-marked-packets"
              label={t("pages.settings.general.skipMarkedPacketsLabel")}
            />

            {advancedMode && (
              <div className="space-y-6 [&_[data-slot=field-description]]:text-amber-700 dark:[&_[data-slot=field-description]]:text-amber-400 [&_[data-slot=field-label]]:text-amber-700 dark:[&_[data-slot=field-label]]:text-amber-400">
                <FieldSeparator />
                <BooleanSettingField
                  field={form.field("iproute.process_router_traffic")}
                  hint={t("pages.settings.general.processRouterTrafficHint")}
                  id="process-router-traffic"
                  label={
                    <span className="inline-flex items-center gap-2">
                      {t("pages.settings.general.processRouterTrafficLabel")}
                      <Toolbox aria-hidden="true" className="size-4 shrink-0" />
                    </span>
                  }
                />
              </div>
            )}

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("daemon.clear_dynamic_sets_on_apply")}
              hint={t("pages.settings.general.clearDynamicSetsOnApplyHint")}
              id="clear-dynamic-sets-on-apply"
              label={t("pages.settings.general.clearDynamicSetsOnApplyLabel")}
            />

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("daemon.ipv6_enabled")}
              hint={t("pages.settings.general.ipv6EnabledHint")}
              id="ipv6-enabled"
              label={t("pages.settings.general.ipv6EnabledLabel")}
            />
          </FieldGroup>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>{t("pages.settings.autoupdate.title")}</CardTitle>
          <CardDescription>
            {t("pages.settings.autoupdate.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <BooleanSettingField
              field={form.field("lists_autoupdate.enabled")}
              hint={t("pages.settings.autoupdate.enabledHint")}
              id="autoupdate-lists"
              label={t("pages.settings.autoupdate.enabledLabel")}
            />

            <FieldSeparator />

            <Field invalid={Boolean(cron.error)}>
              <FieldLabel htmlFor="general-cron">
                {t("pages.settings.autoupdate.cronLabel")}
              </FieldLabel>
              <FieldContent>
                <Input id="general-cron" {...bindInput(cron)} />
                <FieldHint
                  description={
                    <>
                      {t("pages.settings.autoupdate.cronHintPrefix")}{" "}
                      <a
                        className="underline underline-offset-3 hover:text-foreground"
                        href={getCrontabGuruUrl(cron.value)}
                        rel="noreferrer"
                        target="_blank"
                      >
                        Crontab Guru
                      </a>{" "}
                      {t("pages.settings.autoupdate.cronHintSuffix")}
                    </>
                  }
                  error={
                    cron.error ? (
                      <>
                        {cron.error}{" "}
                        <a
                          className="underline underline-offset-3 hover:text-foreground"
                          href={getCrontabGuruUrl(cron.value)}
                          rel="noreferrer"
                          target="_blank"
                        >
                          {t("pages.settings.autoupdate.openInGuru")}
                        </a>
                        .
                      </>
                    ) : null
                  }
                />
              </FieldContent>
            </Field>
          </FieldGroup>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>{t("pages.settings.advanced.title")}</CardTitle>
          <CardDescription>
            {t("pages.settings.advanced.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <AdvancedTextField
              field={form.field("daemon.ipset_maxelem")}
              hint={
                usesNftables ? (
                  <span className="text-muted-foreground">
                    {t("pages.settings.advanced.ipsetIptablesOnlyHint")}
                  </span>
                ) : (
                  t("pages.settings.advanced.ipsetMaxelemHint")
                )
              }
              id="ipset-maxelem"
              inputProps={{
                disabled: usesNftables,
                inputMode: "numeric",
                max: 4294967295,
                min: 1,
                placeholder: "65536",
                type: "number",
              }}
              label={t("pages.settings.advanced.ipsetMaxelemLabel")}
            />
            {advancedMode && (
              <div className="space-y-6 [&_[data-slot=field-description]]:text-amber-700 dark:[&_[data-slot=field-description]]:text-amber-400 [&_[data-slot=field-label]]:text-amber-700 dark:[&_[data-slot=field-label]]:text-amber-400">
                <FieldSeparator />
                <AdvancedTextField
                  field={form.field("daemon.ipset_hashsize")}
                  hint={
                    usesNftables ? (
                      <span className="text-muted-foreground">
                        {t("pages.settings.advanced.ipsetIptablesOnlyHint")}
                      </span>
                    ) : (
                      t("pages.settings.advanced.ipsetHashsizeHint")
                    )
                  }
                  id="ipset-hashsize"
                  inputProps={{
                    disabled: usesNftables,
                    inputMode: "numeric",
                    max: 2147483648,
                    min: 1,
                    placeholder: "1024",
                    type: "number",
                  }}
                  label={
                    <span className="inline-flex items-center gap-2">
                      {t("pages.settings.advanced.ipsetHashsizeLabel")}
                      <Toolbox aria-hidden="true" className="size-4 shrink-0" />
                    </span>
                  }
                />
                <FieldSeparator />
                <div className="grid gap-6 md:grid-cols-2">
                  <AdvancedTextField
                    field={form.field("fwmark.start")}
                    hint={t("pages.settings.advanced.fwmarkStartHint")}
                    id="fwmark-start"
                    label={
                      <span className="inline-flex items-center gap-2">
                        {t("pages.settings.advanced.fwmarkStartLabel")}
                        <Toolbox
                          aria-hidden="true"
                          className="size-4 shrink-0"
                        />
                      </span>
                    }
                  />
                  <AdvancedTextField
                    field={form.field("fwmark.mask")}
                    hint={
                      <>
                        {t("pages.settings.advanced.fwmarkMaskHintPrefix")}{" "}
                        <code>f</code>{" "}
                        {t("pages.settings.advanced.fwmarkMaskHintSuffix")}{" "}
                        <code>0x00ff0000</code>.
                      </>
                    }
                    id="fwmark-mask"
                    label={
                      <span className="inline-flex items-center gap-2">
                        {t("pages.settings.advanced.fwmarkMaskLabel")}
                        <Toolbox
                          aria-hidden="true"
                          className="size-4 shrink-0"
                        />
                      </span>
                    }
                  />
                </div>
                <FieldSeparator />
                <AdvancedTextField
                  field={form.field("iproute.table_start")}
                  hint={t("pages.settings.advanced.tableStartHint")}
                  id="table-start"
                  label={
                    <span className="inline-flex items-center gap-2">
                      {t("pages.settings.advanced.tableStartLabel")}
                      <Toolbox aria-hidden="true" className="size-4 shrink-0" />
                    </span>
                  }
                />
              </div>
            )}
          </FieldGroup>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>{t("pages.settings.dns.title")}</CardTitle>
          <CardDescription>
            {t("pages.settings.dns.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <Field orientation="horizontal">
            <FieldContent>
              <FieldLabel htmlFor="dnsmasq-management">
                {t("pages.settings.dns.resolverIntegrationLabel")}
              </FieldLabel>
              <FieldHint
                description={t("pages.settings.dns.resolverIntegrationHint")}
              />
            </FieldContent>
            <Switch
              checked={form.values.dns.resolver_integration === "dnsmasq"}
              className="scroll-mt-24"
              id="dnsmasq-management"
              onCheckedChange={(enabled) =>
                form.setValue(
                  "dns.resolver_integration",
                  enabled ? "dnsmasq" : "none"
                )
              }
            />
          </Field>
        </CardContent>
      </Card>

      <ServerValidationAlert errors={form.unmappedErrors()} />

      <div className="flex justify-end gap-2">
        <Button
          disabled={isPending}
          onClick={handleCancel}
          size="xl"
          variant="outline"
        >
          {t("common.cancel")}
        </Button>
        <Button
          disabled={isPending || !form.isDirty}
          onClick={() => void form.submit(save)}
          size="xl"
        >
          {isPending
            ? t("pages.settings.actions.saving")
            : t("pages.settings.actions.save")}
        </Button>
      </div>
    </>
  )
}

function GeneralConfigPageSkeleton() {
  return (
    <>
      <Card>
        <CardHeader>
          <Skeleton className="h-6 w-28" />
          <Skeleton className="h-4 w-56" />
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            <Skeleton className="h-4 w-28" />
            <Skeleton className="h-10 w-full" />
            <Skeleton className="h-4 w-full max-w-2xl" />
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <Skeleton className="h-6 w-40" />
          <Skeleton className="h-4 w-64" />
        </CardHeader>
        <CardContent>
          <div className="space-y-6">
            <div className="flex items-start gap-3">
              <Skeleton className="mt-0.5 h-4 w-4 rounded-sm" />
              <div className="space-y-2">
                <Skeleton className="h-4 w-44" />
                <Skeleton className="h-4 w-full max-w-3xl" />
              </div>
            </div>
            <Skeleton className="h-px w-full" />
            <div className="space-y-3">
              <Skeleton className="h-4 w-14" />
              <Skeleton className="h-10 w-full" />
              <Skeleton className="h-4 w-80" />
            </div>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <Skeleton className="h-6 w-56" />
          <Skeleton className="h-4 w-72" />
        </CardHeader>
        <CardContent>
          <div className="space-y-6">
            {[0, 1, 2].map((index) => (
              <div className="space-y-6" key={index}>
                {index > 0 ? <Skeleton className="h-px w-full" /> : null}
                <div className="space-y-3">
                  <Skeleton className="h-4 w-52" />
                  <Skeleton className="h-10 w-full" />
                  <Skeleton className="h-4 w-full max-w-2xl" />
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <div className="flex justify-end gap-2">
        <Skeleton className="h-11 w-24" />
        <Skeleton className="h-11 w-24" />
      </div>
    </>
  )
}

function BooleanSettingField({
  field,
  hint,
  id,
  label,
}: {
  field: FieldBinding<boolean>
  hint?: string
  id: string
  label: ReactNode
}) {
  return (
    <Field>
      <FieldContent>
        <div className="flex items-center space-x-3">
          <Checkbox
            checked={field.value}
            id={id}
            onCheckedChange={(value) => field.onChange(value === true)}
          />
          <FieldLabel
            className="cursor-pointer flex-col items-start gap-0"
            htmlFor={id}
          >
            {label}
          </FieldLabel>
        </div>
        {hint ? <FieldHint description={hint} /> : null}
      </FieldContent>
    </Field>
  )
}

function TextSettingField({
  field,
  hint,
  id,
  label,
}: {
  field: FieldBinding<string>
  hint?: string
  id: string
  label: ReactNode
}) {
  return (
    <Field invalid={Boolean(field.error)}>
      <FieldLabel htmlFor={id}>{label}</FieldLabel>
      <FieldContent>
        <Input id={id} {...bindInput(field)} />
        <FieldHint description={hint} error={field.error} />
      </FieldContent>
    </Field>
  )
}

function NumberSettingField({
  field,
  hint,
  id,
  label,
}: {
  field: FieldBinding<string>
  hint?: string
  id: string
  label: ReactNode
}) {
  return (
    <Field invalid={Boolean(field.error)}>
      <FieldLabel htmlFor={id}>{label}</FieldLabel>
      <FieldContent>
        <Input
          id={id}
          inputMode="numeric"
          type="number"
          {...bindInput(field)}
        />
        <FieldHint description={hint} error={field.error} />
      </FieldContent>
    </Field>
  )
}

function AdvancedTextField({
  field,
  hint,
  id,
  inputProps,
  label,
}: {
  field: FieldBinding<string>
  hint: ReactNode
  id: string
  inputProps?: ComponentProps<typeof Input>
  label: ReactNode
}) {
  return (
    <Field invalid={Boolean(field.error)}>
      <FieldLabel htmlFor={id}>{label}</FieldLabel>
      <FieldContent>
        <Input id={id} {...inputProps} {...bindInput(field)} />
        <FieldHint description={hint} error={field.error} />
      </FieldContent>
    </Field>
  )
}

// eslint-disable-next-line react-refresh/only-export-components
export function getDraftFromConfig(config: ConfigObject): SettingsDraft {
  const intercept = config.intercept
  const dns = intercept?.dns
  const marker = dns?.marker
  const l7 = intercept?.l7
  const fallback = fallbackDraft
  return {
    device_name: config.device_name ?? fallback.device_name,
    daemon: {
      strict_enforcement:
        config.daemon?.strict_enforcement ?? fallback.daemon.strict_enforcement,
      skip_marked_packets:
        config.daemon?.skip_marked_packets ??
        fallback.daemon.skip_marked_packets,
      clear_dynamic_sets_on_apply:
        config.daemon?.clear_dynamic_sets_on_apply ??
        fallback.daemon.clear_dynamic_sets_on_apply,
      ipv6_enabled: config.daemon?.ipv6_enabled ?? fallback.daemon.ipv6_enabled,
      ipset_hashsize: toStringInt(config.daemon?.ipset_hashsize, ""),
      ipset_maxelem: toStringInt(config.daemon?.ipset_maxelem, ""),
    },
    route: {
      inbound_interfaces:
        config.route?.inbound_interfaces ?? fallback.route.inbound_interfaces,
    },
    lists_autoupdate: {
      enabled:
        config.lists_autoupdate?.enabled ?? fallback.lists_autoupdate.enabled,
      cron: config.lists_autoupdate?.cron ?? fallback.lists_autoupdate.cron,
    },
    fwmark: {
      start: toHex32(config.fwmark?.start, fallback.fwmark.start),
      mask: toHex32(config.fwmark?.mask, fallback.fwmark.mask),
    },
    iproute: {
      table_start: toStringInt(
        config.iproute?.table_start,
        fallback.iproute.table_start
      ),
      process_router_traffic:
        config.iproute?.process_router_traffic ??
        fallback.iproute.process_router_traffic,
    },
    dns: {
      resolver_integration: effectiveResolverIntegration(config),
    },
    intercept: {
      enabled: intercept?.enabled ?? fallback.intercept.enabled,
      min_ttl_s: toStringInt(
        intercept?.min_ttl_s,
        fallback.intercept.min_ttl_s
      ),
      max_ttl_s: toStringInt(
        intercept?.max_ttl_s,
        fallback.intercept.max_ttl_s
      ),
      dns: {
        enabled: dns?.enabled ?? fallback.intercept.dns.enabled,
        queue_num: toStringInt(
          dns?.queue_num,
          fallback.intercept.dns.queue_num
        ),
        hold_timeout_ms: toStringInt(
          dns?.hold_timeout_ms,
          fallback.intercept.dns.hold_timeout_ms
        ),
        marker: {
          domain: marker?.domain ?? fallback.intercept.dns.marker.domain,
          answer_ipv4:
            marker?.answer_ipv4 ?? fallback.intercept.dns.marker.answer_ipv4,
        },
      },
      l7: {
        enabled: l7?.enabled ?? fallback.intercept.l7.enabled,
        nflog_group: toStringInt(
          l7?.nflog_group,
          fallback.intercept.l7.nflog_group
        ),
        tls: l7?.tls ?? fallback.intercept.l7.tls,
        http: l7?.http ?? fallback.intercept.l7.http,
        quic: l7?.quic ?? fallback.intercept.l7.quic,
      },
    },
  }
}

// eslint-disable-next-line react-refresh/only-export-components
export function buildUpdatedConfig(
  config: ConfigObject,
  draft: SettingsDraft
): ConfigObject {
  const { daemon, intercept } = draft
  const resolverIntegration = draft.dns.resolver_integration
  const tableStart = parseStrictDecimalToNumber(draft.iproute.table_start)
  const marker = {
    ...config.intercept?.dns?.marker,
    domain: intercept.dns.marker.domain.trim(),
    answer_ipv4: intercept.dns.marker.answer_ipv4.trim(),
  }

  return {
    ...config,
    device_name: draft.device_name.trim(),
    daemon: {
      ...config.daemon,
      strict_enforcement: daemon.strict_enforcement,
      skip_marked_packets: daemon.skip_marked_packets,
      clear_dynamic_sets_on_apply: daemon.clear_dynamic_sets_on_apply,
      ipv6_enabled: daemon.ipv6_enabled,
      ipset_hashsize: toOptionalBackendInteger(daemon.ipset_hashsize),
      ipset_maxelem: toOptionalBackendInteger(daemon.ipset_maxelem),
    },
    route: {
      ...config.route,
      inbound_interfaces: draft.route.inbound_interfaces,
    },
    fwmark: {
      ...config.fwmark,
      start: draft.fwmark.start.trim(),
      mask: draft.fwmark.mask.trim(),
    },
    iproute: {
      ...config.iproute,
      table_start: toBackendIntegerValue(
        tableStart,
        draft.iproute.table_start.trim()
      ),
      process_router_traffic: draft.iproute.process_router_traffic,
    },
    lists_autoupdate: {
      ...config.lists_autoupdate,
      enabled: draft.lists_autoupdate.enabled,
      cron: draft.lists_autoupdate.cron.trim(),
    },
    dns: {
      ...config.dns,
      ...(effectiveResolverIntegration(config) === resolverIntegration
        ? {}
        : { resolver_integration: resolverIntegration }),
    },
    intercept: {
      ...config.intercept,
      enabled: intercept.enabled,
      min_ttl_s: toOptionalBackendInteger(intercept.min_ttl_s),
      max_ttl_s: toOptionalBackendInteger(intercept.max_ttl_s),
      dns: {
        ...config.intercept?.dns,
        enabled: intercept.dns.enabled,
        queue_num: toOptionalBackendInteger(intercept.dns.queue_num),
        hold_timeout_ms: toOptionalBackendInteger(
          intercept.dns.hold_timeout_ms
        ),
        marker,
      },
      l7: {
        ...config.intercept?.l7,
        enabled: intercept.l7.enabled,
        nflog_group: toOptionalBackendInteger(intercept.l7.nflog_group),
        tls: intercept.l7.tls,
        http: intercept.l7.http,
        quic: intercept.l7.quic,
      },
    },
  }
}

function toHex32(value: string | undefined, fallback: string) {
  if (!value) {
    return fallback
  }

  const trimmed = value.trim()
  if (!/^0x[0-9a-fA-F]+$/.test(trimmed)) {
    return fallback
  }

  const normalized = trimmed.slice(2).replace(/^0+/, "") || "0"
  return `0x${normalized.padStart(8, "0")}`
}

function toStringInt(value: number | null | undefined, fallback: string) {
  if (!Number.isInteger(value)) {
    return fallback
  }

  return String(value)
}

function parseStrictDecimalToNumber(value: string) {
  const trimmed = value.trim()
  if (!/^\d+$/.test(trimmed)) {
    return null
  }

  return Number.parseInt(trimmed, 10)
}

function toBackendIntegerValue(parsed: number | null, raw: string): number {
  if (parsed !== null) {
    return parsed
  }

  return raw as unknown as number
}

function toOptionalBackendInteger(raw: string): number | undefined {
  if (!raw.trim()) {
    return undefined
  }

  const parsed = parseStrictDecimalToNumber(raw)
  return parsed ?? (raw as unknown as number)
}

function getCrontabGuruUrl(value: string) {
  if (getCronHash(value) === null) {
    return "https://crontab.guru/"
  }

  return `https://crontab.guru/#${getCronHash(value)}`
}

function getCronHash(value: string) {
  const trimmed = value.trim()
  if (!trimmed) {
    return null
  }

  const fields = trimmed.split(/\s+/)
  if (fields.length !== 5) {
    return null
  }

  return fields.join("_")
}
