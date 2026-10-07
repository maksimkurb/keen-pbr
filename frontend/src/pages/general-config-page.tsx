import type { ComponentProps, ReactNode } from "react"
import { useTranslation } from "react-i18next"

import { useQueryClient } from "@tanstack/react-query"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig, useGetRuntimeInterfaces } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
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
import { Skeleton } from "@/components/ui/skeleton"
import {
  type FieldBinding,
  bindInput,
  splitFormApiErrors,
  useDraftForm,
} from "@/lib/draft-form"
import { toast } from "sonner"

export type SettingsDraft = {
  deviceName: string
  strictEnforcement: boolean
  skipMarkedPackets: boolean
  processRouterTraffic: boolean
  clearDynamicSetsOnApply: boolean
  ipv6Enabled: boolean
  ipsetHashsize: string
  ipsetMaxelem: string
  inboundInterfaces: string[]
  listsAutoupdateEnabled: boolean
  cron: string
  fwmarkStart: string
  fwmarkMask: string
  tableStart: string
  interceptEnabled: boolean
  interceptMinTtlS: string
  interceptMaxTtlS: string
  interceptDnsEnabled: boolean
  interceptDnsQueueNum: string
  interceptDnsHoldTimeoutMs: string
  interceptMarkerDomain: string
  interceptMarkerAddress: string
  interceptL7Enabled: boolean
  interceptL7NflogGroup: string
  interceptL7Tls: boolean
  interceptL7Http: boolean
  interceptL7Quic: boolean
}

const fallbackDraft: SettingsDraft = {
  deviceName: "",
  strictEnforcement: true,
  skipMarkedPackets: true,
  processRouterTraffic: false,
  clearDynamicSetsOnApply: false,
  ipv6Enabled: true,
  ipsetHashsize: "",
  ipsetMaxelem: "",
  inboundInterfaces: [],
  listsAutoupdateEnabled: false,
  cron: "0 4 * * 0",
  fwmarkStart: "0x00010000",
  fwmarkMask: "0xffff0000",
  tableStart: "150",
  interceptEnabled: true,
  interceptMinTtlS: "300",
  interceptMaxTtlS: "86400",
  interceptDnsEnabled: true,
  interceptDnsQueueNum: "9053",
  interceptDnsHoldTimeoutMs: "30",
  interceptMarkerDomain: "check.keen.pbr",
  interceptMarkerAddress: "127.0.0.88",
  interceptL7Enabled: true,
  interceptL7NflogGroup: "9054",
  interceptL7Tls: true,
  interceptL7Http: true,
  interceptL7Quic: true,
}

const SETTINGS_FIELD_NAMES = {
  deviceName: "deviceName",
  strictEnforcement: "strictEnforcement",
  skipMarkedPackets: "skipMarkedPackets",
  processRouterTraffic: "processRouterTraffic",
  clearDynamicSetsOnApply: "clearDynamicSetsOnApply",
  ipv6Enabled: "ipv6Enabled",
  ipsetHashsize: "ipsetHashsize",
  ipsetMaxelem: "ipsetMaxelem",
  inboundInterfaces: "inboundInterfaces",
  listsAutoupdateEnabled: "listsAutoupdateEnabled",
  cron: "cron",
  fwmarkStart: "fwmarkStart",
  fwmarkMask: "fwmarkMask",
  tableStart: "tableStart",
  interceptEnabled: "interceptEnabled",
  interceptMinTtlS: "interceptMinTtlS",
  interceptMaxTtlS: "interceptMaxTtlS",
  interceptDnsEnabled: "interceptDnsEnabled",
  interceptDnsQueueNum: "interceptDnsQueueNum",
  interceptDnsHoldTimeoutMs: "interceptDnsHoldTimeoutMs",
  interceptMarkerDomain: "interceptMarkerDomain",
  interceptMarkerAddress: "interceptMarkerAddress",
  interceptL7Enabled: "interceptL7Enabled",
  interceptL7NflogGroup: "interceptL7NflogGroup",
  interceptL7Tls: "interceptL7Tls",
  interceptL7Http: "interceptL7Http",
  interceptL7Quic: "interceptL7Quic",
} as const

type SettingsFieldName =
  (typeof SETTINGS_FIELD_NAMES)[keyof typeof SETTINGS_FIELD_NAMES]

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
  const queryClient = useQueryClient()
  const runtimeInterfacesQuery = useGetRuntimeInterfaces()

  const postConfigMutation = usePostConfigMutation()

  const form = useDraftForm<SettingsDraft>(getDraftFromConfig(loadedConfig))

  const save = async (value: SettingsDraft) => {
    const updatedConfig = buildUpdatedConfig(loadedConfig, value)

    try {
      await postConfigMutation.mutateAsync({ data: updatedConfig })
    } catch (error) {
      const result = splitFormApiErrors({
        error: error as ApiError,
        fieldNames: Object.values(SETTINGS_FIELD_NAMES),
        resolvePath: resolveSettingsFieldPath,
      })

      form.setServerErrors({
        form: result.formError,
        fields: result.fieldErrors,
        unmapped: result.unmappedErrors,
      })

      if (result.formError) {
        toast.error(result.formError, { richColors: true })
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

  const unmappedServerErrors = form.errors.unmapped

  const isPending = form.isSubmitting
  const runtimeInterfaces =
    runtimeInterfacesQuery.data?.status === 200
      ? runtimeInterfacesQuery.data.data.interfaces
      : []

  const handleCancel = () => {
    form.reset(getDraftFromConfig(loadedConfig))
  }

  const cron = form.field("cron")
  const inboundInterfacesError = form.errorFor("inboundInterfaces")

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
              {...bindInput(form.field("deviceName"))}
            />
            <FieldDescription>
              {t("pages.settings.general.deviceNameHint")}
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
              field={form.field("interceptEnabled")}
              id="intercept-enabled"
              label={t("pages.settings.intercept.enabledLabel")}
              hint={t("pages.settings.intercept.enabledHint")}
            />

            <FieldSeparator />

            <div className="grid gap-6 md:grid-cols-2">
              <NumberSettingField
                field={form.field("interceptMinTtlS")}
                id="intercept-min-ttl"
                label={t("pages.settings.intercept.minTtlLabel")}
                hint={t("pages.settings.intercept.minTtlHint")}
              />
              <NumberSettingField
                field={form.field("interceptMaxTtlS")}
                id="intercept-max-ttl"
                label={t("pages.settings.intercept.maxTtlLabel")}
                hint={t("pages.settings.intercept.maxTtlHint")}
              />
            </div>

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("interceptDnsEnabled")}
              id="intercept-dns-enabled"
              label={t("pages.settings.intercept.dnsEnabledLabel")}
              hint={t("pages.settings.intercept.dnsEnabledHint")}
            />

            <div className="grid gap-6 md:grid-cols-2">
              <NumberSettingField
                field={form.field("interceptDnsQueueNum")}
                id="intercept-dns-queue"
                label={t("pages.settings.intercept.queueLabel")}
                hint={t("pages.settings.intercept.queueHint")}
              />
              <NumberSettingField
                field={form.field("interceptDnsHoldTimeoutMs")}
                id="intercept-dns-timeout"
                label={t("pages.settings.intercept.holdTimeoutLabel")}
                hint={t("pages.settings.intercept.holdTimeoutHint")}
              />
            </div>

            <div className="grid gap-6 md:grid-cols-2">
              <TextSettingField
                field={form.field("interceptMarkerDomain")}
                id="intercept-marker-domain"
                label={t("pages.settings.intercept.markerDomainLabel")}
                hint={t("pages.settings.intercept.markerDomainHint")}
              />
              <TextSettingField
                field={form.field("interceptMarkerAddress")}
                id="intercept-marker-address"
                label={t("pages.settings.intercept.markerAddressLabel")}
                hint={t("pages.settings.intercept.markerAddressHint")}
              />
            </div>

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("interceptL7Enabled")}
              id="intercept-l7-enabled"
              label={t("pages.settings.intercept.l7EnabledLabel")}
              hint={t("pages.settings.intercept.l7EnabledHint")}
            />

            <NumberSettingField
              field={form.field("interceptL7NflogGroup")}
              id="intercept-l7-group"
              label={t("pages.settings.intercept.nflogGroupLabel")}
              hint={t("pages.settings.intercept.nflogGroupHint")}
            />

            <div className="grid gap-6 md:grid-cols-3">
              <BooleanSettingField
                field={form.field("interceptL7Tls")}
                id="intercept-l7-tls"
                label={t("pages.settings.intercept.tlsLabel")}
              />
              <BooleanSettingField
                field={form.field("interceptL7Http")}
                id="intercept-l7-http"
                label={t("pages.settings.intercept.httpLabel")}
              />
              <BooleanSettingField
                field={form.field("interceptL7Quic")}
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
            <BooleanSettingField
              field={form.field("strictEnforcement")}
              hint={t("pages.settings.general.strictEnforcementHint")}
              id="strict-enforcement"
              label={t("pages.settings.general.strictEnforcementLabel")}
            />

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("skipMarkedPackets")}
              hint={t("pages.settings.general.skipMarkedPacketsHint")}
              id="skip-marked-packets"
              label={t("pages.settings.general.skipMarkedPacketsLabel")}
            />

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("processRouterTraffic")}
              hint={t("pages.settings.general.processRouterTrafficHint")}
              id="process-router-traffic"
              label={t("pages.settings.general.processRouterTrafficLabel")}
            />

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("clearDynamicSetsOnApply")}
              hint={t("pages.settings.general.clearDynamicSetsOnApplyHint")}
              id="clear-dynamic-sets-on-apply"
              label={t("pages.settings.general.clearDynamicSetsOnApplyLabel")}
            />

            <FieldSeparator />

            <BooleanSettingField
              field={form.field("ipv6Enabled")}
              hint={t("pages.settings.general.ipv6EnabledHint")}
              id="ipv6-enabled"
              label={t("pages.settings.general.ipv6EnabledLabel")}
            />

            <FieldSeparator />

            <Field invalid={Boolean(inboundInterfacesError)}>
              <FieldLabel htmlFor="inbound-interfaces">
                {t("pages.settings.general.inboundInterfacesLabel")}
              </FieldLabel>
              <FieldContent>
                <div id="inbound-interfaces">
                  <InterfaceMultiSelectList
                    name={SETTINGS_FIELD_NAMES.inboundInterfaces}
                    interfaces={runtimeInterfaces}
                    value={form.values.inboundInterfaces}
                    onChange={(value) =>
                      form.setValue("inboundInterfaces", value)
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
              field={form.field("listsAutoupdateEnabled")}
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
              field={form.field("fwmarkStart")}
              hint={t("pages.settings.advanced.fwmarkStartHint")}
              id="fwmark-start"
              label={t("pages.settings.advanced.fwmarkStartLabel")}
            />

            <FieldSeparator />

            <AdvancedTextField
              field={form.field("fwmarkMask")}
              hint={
                <>
                  {t("pages.settings.advanced.fwmarkMaskHintPrefix")}{" "}
                  <code>f</code>{" "}
                  {t("pages.settings.advanced.fwmarkMaskHintSuffix")}{" "}
                  <code>0x00ff0000</code>.
                </>
              }
              id="fwmark-mask"
              label={t("pages.settings.advanced.fwmarkMaskLabel")}
            />

            <FieldSeparator />

            <AdvancedTextField
              field={form.field("tableStart")}
              hint={t("pages.settings.advanced.tableStartHint")}
              id="table-start"
              label={t("pages.settings.advanced.tableStartLabel")}
            />

            <FieldSeparator />

            <AdvancedTextField
              field={form.field("ipsetHashsize")}
              hint={t("pages.settings.advanced.ipsetHashsizeHint")}
              id="ipset-hashsize"
              inputProps={{
                inputMode: "numeric",
                max: 2147483648,
                min: 1,
                placeholder: "1024",
                type: "number",
              }}
              label={t("pages.settings.advanced.ipsetHashsizeLabel")}
            />

            <FieldSeparator />

            <AdvancedTextField
              field={form.field("ipsetMaxelem")}
              hint={t("pages.settings.advanced.ipsetMaxelemHint")}
              id="ipset-maxelem"
              inputProps={{
                inputMode: "numeric",
                max: 4294967295,
                min: 1,
                placeholder: "65536",
                type: "number",
              }}
              label={t("pages.settings.advanced.ipsetMaxelemLabel")}
            />
          </FieldGroup>
        </CardContent>
      </Card>

      <ServerValidationAlert errors={unmappedServerErrors} />

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
  label: string
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
  label: string
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
  label: string
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
  label: string
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
  return {
    deviceName: config.device_name ?? fallbackDraft.deviceName,
    strictEnforcement:
      config.daemon?.strict_enforcement ?? fallbackDraft.strictEnforcement,
    skipMarkedPackets:
      config.daemon?.skip_marked_packets ?? fallbackDraft.skipMarkedPackets,
    processRouterTraffic:
      config.iproute?.process_router_traffic ??
      fallbackDraft.processRouterTraffic,
    clearDynamicSetsOnApply:
      config.daemon?.clear_dynamic_sets_on_apply ??
      fallbackDraft.clearDynamicSetsOnApply,
    ipv6Enabled: config.daemon?.ipv6_enabled ?? fallbackDraft.ipv6Enabled,
    ipsetHashsize: toStringInt(config.daemon?.ipset_hashsize, ""),
    ipsetMaxelem: toStringInt(config.daemon?.ipset_maxelem, ""),
    inboundInterfaces:
      config.route?.inbound_interfaces ?? fallbackDraft.inboundInterfaces,
    listsAutoupdateEnabled:
      config.lists_autoupdate?.enabled ?? fallbackDraft.listsAutoupdateEnabled,
    cron: config.lists_autoupdate?.cron ?? fallbackDraft.cron,
    fwmarkStart: toHex32(config.fwmark?.start, fallbackDraft.fwmarkStart),
    fwmarkMask: toHex32(config.fwmark?.mask, fallbackDraft.fwmarkMask),
    tableStart: toStringInt(
      config.iproute?.table_start,
      fallbackDraft.tableStart
    ),
    interceptEnabled: intercept?.enabled ?? fallbackDraft.interceptEnabled,
    interceptMinTtlS: toStringInt(
      intercept?.min_ttl_s,
      fallbackDraft.interceptMinTtlS
    ),
    interceptMaxTtlS: toStringInt(
      intercept?.max_ttl_s,
      fallbackDraft.interceptMaxTtlS
    ),
    interceptDnsEnabled: dns?.enabled ?? fallbackDraft.interceptDnsEnabled,
    interceptDnsQueueNum: toStringInt(
      dns?.queue_num,
      fallbackDraft.interceptDnsQueueNum
    ),
    interceptDnsHoldTimeoutMs: toStringInt(
      dns?.hold_timeout_ms,
      fallbackDraft.interceptDnsHoldTimeoutMs
    ),
    interceptMarkerDomain:
      marker?.domain ?? fallbackDraft.interceptMarkerDomain,
    interceptMarkerAddress:
      marker?.answer_ipv4 ?? fallbackDraft.interceptMarkerAddress,
    interceptL7Enabled: l7?.enabled ?? fallbackDraft.interceptL7Enabled,
    interceptL7NflogGroup: toStringInt(
      l7?.nflog_group,
      fallbackDraft.interceptL7NflogGroup
    ),
    interceptL7Tls: l7?.tls ?? fallbackDraft.interceptL7Tls,
    interceptL7Http: l7?.http ?? fallbackDraft.interceptL7Http,
    interceptL7Quic: l7?.quic ?? fallbackDraft.interceptL7Quic,
  }
}

// eslint-disable-next-line react-refresh/only-export-components
export function buildUpdatedConfig(
  config: ConfigObject,
  draft: SettingsDraft
): ConfigObject {
  const tableStart = parseStrictDecimalToNumber(draft.tableStart)
  const marker = {
    ...config.intercept?.dns?.marker,
    domain: draft.interceptMarkerDomain.trim(),
    answer_ipv4: draft.interceptMarkerAddress.trim(),
  }

  return {
    ...config,
    device_name: draft.deviceName.trim(),
    daemon: {
      ...config.daemon,
      strict_enforcement: draft.strictEnforcement,
      skip_marked_packets: draft.skipMarkedPackets,
      clear_dynamic_sets_on_apply: draft.clearDynamicSetsOnApply,
      ipv6_enabled: draft.ipv6Enabled,
      ipset_hashsize: toOptionalBackendInteger(draft.ipsetHashsize),
      ipset_maxelem: toOptionalBackendInteger(draft.ipsetMaxelem),
    },
    route: {
      ...config.route,
      inbound_interfaces: draft.inboundInterfaces,
    },
    fwmark: {
      ...config.fwmark,
      start: draft.fwmarkStart.trim(),
      mask: draft.fwmarkMask.trim(),
    },
    iproute: {
      ...config.iproute,
      table_start: toBackendIntegerValue(tableStart, draft.tableStart.trim()),
      process_router_traffic: draft.processRouterTraffic,
    },
    lists_autoupdate: {
      ...config.lists_autoupdate,
      enabled: draft.listsAutoupdateEnabled,
      cron: draft.cron.trim(),
    },
    dns: {
      ...config.dns,
    },
    intercept: {
      ...config.intercept,
      enabled: draft.interceptEnabled,
      min_ttl_s: toOptionalBackendInteger(draft.interceptMinTtlS),
      max_ttl_s: toOptionalBackendInteger(draft.interceptMaxTtlS),
      dns: {
        ...config.intercept?.dns,
        enabled: draft.interceptDnsEnabled,
        queue_num: toOptionalBackendInteger(draft.interceptDnsQueueNum),
        hold_timeout_ms: toOptionalBackendInteger(
          draft.interceptDnsHoldTimeoutMs
        ),
        marker,
      },
      l7: {
        ...config.intercept?.l7,
        enabled: draft.interceptL7Enabled,
        nflog_group: toOptionalBackendInteger(draft.interceptL7NflogGroup),
        tls: draft.interceptL7Tls,
        http: draft.interceptL7Http,
        quic: draft.interceptL7Quic,
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

function resolveSettingsFieldPath(path: string): SettingsFieldName | undefined {
  if (
    path === "route.inbound_interfaces" ||
    path.startsWith("route.inbound_interfaces[")
  ) {
    return SETTINGS_FIELD_NAMES.inboundInterfaces
  }

  switch (path) {
    case "device_name":
      return SETTINGS_FIELD_NAMES.deviceName
    case "daemon.strict_enforcement":
      return SETTINGS_FIELD_NAMES.strictEnforcement
    case "daemon.skip_marked_packets":
      return SETTINGS_FIELD_NAMES.skipMarkedPackets
    case "daemon.clear_dynamic_sets_on_apply":
      return SETTINGS_FIELD_NAMES.clearDynamicSetsOnApply
    case "daemon.ipv6_enabled":
      return SETTINGS_FIELD_NAMES.ipv6Enabled
    case "daemon.ipset_hashsize":
      return SETTINGS_FIELD_NAMES.ipsetHashsize
    case "daemon.ipset_maxelem":
      return SETTINGS_FIELD_NAMES.ipsetMaxelem
    case "lists_autoupdate.enabled":
      return SETTINGS_FIELD_NAMES.listsAutoupdateEnabled
    case "lists_autoupdate.cron":
      return SETTINGS_FIELD_NAMES.cron
    case "fwmark.start":
      return SETTINGS_FIELD_NAMES.fwmarkStart
    case "fwmark.mask":
      return SETTINGS_FIELD_NAMES.fwmarkMask
    case "iproute.process_router_traffic":
      return SETTINGS_FIELD_NAMES.processRouterTraffic
    case "iproute.table_start":
      return SETTINGS_FIELD_NAMES.tableStart
    case "intercept.enabled":
      return SETTINGS_FIELD_NAMES.interceptEnabled
    case "intercept.min_ttl_s":
      return SETTINGS_FIELD_NAMES.interceptMinTtlS
    case "intercept.max_ttl_s":
      return SETTINGS_FIELD_NAMES.interceptMaxTtlS
    case "intercept.dns.enabled":
      return SETTINGS_FIELD_NAMES.interceptDnsEnabled
    case "intercept.dns.queue_num":
      return SETTINGS_FIELD_NAMES.interceptDnsQueueNum
    case "intercept.dns.hold_timeout_ms":
      return SETTINGS_FIELD_NAMES.interceptDnsHoldTimeoutMs
    case "intercept.dns.marker.domain":
      return SETTINGS_FIELD_NAMES.interceptMarkerDomain
    case "intercept.dns.marker.answer_ipv4":
      return SETTINGS_FIELD_NAMES.interceptMarkerAddress
    case "intercept.l7.enabled":
      return SETTINGS_FIELD_NAMES.interceptL7Enabled
    case "intercept.l7.nflog_group":
      return SETTINGS_FIELD_NAMES.interceptL7NflogGroup
    case "intercept.l7.tls":
      return SETTINGS_FIELD_NAMES.interceptL7Tls
    case "intercept.l7.http":
      return SETTINGS_FIELD_NAMES.interceptL7Http
    case "intercept.l7.quic":
      return SETTINGS_FIELD_NAMES.interceptL7Quic
    default:
      return undefined
  }
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
