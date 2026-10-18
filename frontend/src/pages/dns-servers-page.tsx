import { DnsManagementDisabled } from "@/components/shared/dns-management-disabled"
import { ArrowRight, Pencil, Plus, Trash2 } from "lucide-react"
import type { ReactNode } from "react"
import { useEffect, useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { toast } from "sonner"
import { useLocation } from "wouter"

import { useQueryClient } from "@tanstack/react-query"

import type { ApiError } from "@/api/client"
import type { getConfigResponse } from "@/api/generated/keen-api"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { DnsCheckResponse } from "@/api/generated/model/dnsCheckResponse"
import type { DnsRule } from "@/api/generated/model/dnsRule"
import { DnsServerType } from "@/api/generated/model/dnsServerType"
import {
  useConfigMutationPending,
  usePostConfigMutation,
} from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { usePostDnsServersCheck } from "@/api/generated/keen-api"
import { getApiErrorMessage } from "@/lib/api-errors"
import {
  effectiveResolverIntegration,
  selectConfigIsDraft,
} from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { MobileAddFab } from "@/components/shared/mobile-add-fab"
import { BulkSelectionToolbar } from "@/components/shared/bulk-selection-toolbar"
import { ConfigSaveErrorAlert } from "@/components/shared/config-save-error-alert"
import { DataTable } from "@/components/shared/data-table"
import {
  DeleteImpactDialog,
  type DeleteImpactItem,
} from "@/components/shared/delete-impact-dialog"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { useRowSelection } from "@/hooks/use-row-selection"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { DnsServerLists } from "@/pages/dns-servers-lists"
import {
  DnsCheckBadge,
  DnsCheckButton,
  DnsCheckResultAlert,
} from "@/pages/dns-servers-check"
import {
  buildCheckRequest,
  loadSelection,
  saveSelection,
  type CheckSelection,
} from "@/pages/dns-servers-check-utils"
import { getRuleDraft, validateRules } from "@/pages/dns-rules-utils"
import {
  buildUpdatedConfigForDnsServersDelete,
  buildUpdatedConfigWithServerList,
  getBootstrapCandidateTags,
  getDnsServerDeleteImpact,
  type DnsServerDeleteImpact,
} from "@/pages/dns-servers-utils"

export function DnsServersPage() {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const [deleteRequest, setDeleteRequest] = useState<{
    tags: string[]
    impact: DnsServerDeleteImpact
    config: ConfigObject
    clearSelectionOnSuccess: boolean
  } | null>(null)
  const [deletePreview, setDeletePreview] = useState<typeof deleteRequest>(null)
  const configMutationPending = useConfigMutationPending()
  const configQuery = useGetConfig()
  const postConfigMutation = usePostConfigMutation()

  const checkMutation = usePostDnsServersCheck()
  const [checkSelection, setCheckSelection] = useState<CheckSelection>(() =>
    loadSelection()
  )
  const resetCheck = checkMutation.reset
  // The result describes the settings that were applied when it was made.
  useEffect(() => {
    resetCheck()
  }, [configQuery.dataUpdatedAt, resetCheck])

  const changeCheckSelection = (next: CheckSelection) => {
    setCheckSelection(next)
    saveSelection(next)
  }
  const runCheck = () => {
    checkMutation.mutate({ data: buildCheckRequest(checkSelection) })
  }
  const checkResponse =
    checkMutation.data?.status === 200 ? checkMutation.data.data : undefined
  const checkErrorMessage = checkMutation.isError
    ? getApiErrorMessage(checkMutation.error as unknown as ApiError | null)
    : undefined

  const config = getConfigData(configQuery.data)
  const visibleDeleteRequest = deleteRequest ?? deletePreview
  const dnsServers = useMemo(() => config?.dns?.servers ?? [], [config])
  const serverRowIds = dnsServers.map((server) => server.tag)
  const serverSelection = useRowSelection(serverRowIds)
  const bootstrapOptions = useMemo(
    () => getBootstrapCandidateTags(dnsServers),
    [dnsServers]
  )

  const saveServerList = (key: "fallback" | "bootstrap", tags: string[]) => {
    if (!config) {
      return
    }

    const knownTags = key === "bootstrap" ? bootstrapOptions : serverRowIds
    if (tags.some((tag) => !knownTags.includes(tag))) {
      toast.error(
        t(
          key === "bootstrap"
            ? "pages.dnsServers.validation.invalidBootstrap"
            : "pages.dnsServers.validation.invalidFallback"
        ),
        { richColors: true }
      )
      return
    }

    if (key === "fallback") {
      const draftRules = (config.dns?.rules ?? []).map((rule) =>
        getRuleDraft(rule)
      )
      const listOptions = Object.keys(config.lists ?? {})
      if (
        Object.keys(validateRules(draftRules, serverRowIds, listOptions))
          .length > 0
      ) {
        toast.error(t("pages.dnsServers.validation.invalidFallbackChange"), {
          richColors: true,
        })
        return
      }
    }

    postConfigMutation.mutate(
      { data: buildUpdatedConfigWithServerList(config, key, tags) },
      {
        onSuccess: async () => {
          await queryClient.invalidateQueries({ queryKey: queryKeys.dnsTest() })
          toast.success(t("pages.dnsServers.messages.saved"))
        },
        onError: (error) => {
          toast.error(getApiErrorMessage(error as unknown as ApiError), {
            richColors: true,
          })
        },
      }
    )
  }

  const deleteServer = (serverTag: string) => {
    if (!config) {
      return
    }

    const request = {
      tags: [serverTag],
      impact: getDnsServerDeleteImpact(config, [serverTag]),
      config,
      clearSelectionOnSuccess: false,
    }
    setDeletePreview(request)
    setDeleteRequest(request)
  }

  const deleteServersBulk = () => {
    if (!config || serverSelection.selectedCount === 0) {
      return
    }

    const selectedTags = [...serverSelection.selectedIds]

    const request = {
      tags: selectedTags,
      impact: getDnsServerDeleteImpact(config, selectedTags),
      config,
      clearSelectionOnSuccess: true,
    }
    setDeletePreview(request)
    setDeleteRequest(request)
  }

  const confirmDelete = () => {
    if (!config || !deleteRequest) {
      return
    }

    const updatedConfig = buildUpdatedConfigForDnsServersDelete(
      config,
      deleteRequest.tags,
      true
    )

    postConfigMutation.mutate(
      { data: updatedConfig },
      {
        onSuccess: () => {
          if (deleteRequest.clearSelectionOnSuccess) {
            serverSelection.clear()
          }
          setDeleteRequest(null)
        },
      }
    )
  }

  return (
    <div className="space-y-6 pb-20 md:pb-0">
      <PageHeader
        actions={
          effectiveResolverIntegration(config) === "dnsmasq" ? (
            <div className="flex flex-wrap items-center gap-2">
              <DnsCheckButton
                disabled={configMutationPending || dnsServers.length === 0}
                isRunning={checkMutation.isPending}
                onRun={runCheck}
                onSelectionChange={changeCheckSelection}
                selection={checkSelection}
              />
              <Button
                className="hidden md:inline-flex"
                disabled={configMutationPending}
                onClick={() => navigate("/dns-servers/create")}
              >
                <Plus className="mr-1 h-4 w-4" />
                {t("pages.dnsServers.actions.add")}
              </Button>
              <MobileAddFab
                disabled={configMutationPending}
                icon={<Plus className="size-4" />}
                onClick={() => navigate("/dns-servers/create")}
              />
            </div>
          ) : null
        }
        description={t("pages.dnsServers.description")}
        title={t("pages.dnsServers.title")}
      />

      <ConfigSaveErrorAlert error={postConfigMutation.error} />
      {checkResponse ? (
        <DnsCheckResultAlert
          draft={selectConfigIsDraft(configQuery.data)}
          response={checkResponse}
        />
      ) : null}
      {checkErrorMessage ? (
        <Alert variant="destructive">
          <AlertDescription>
            {t("pages.dnsServers.check.result.failed", {
              message: checkErrorMessage,
            })}
          </AlertDescription>
        </Alert>
      ) : null}

      {configQuery.isLoading ? (
        <TableSkeleton />
      ) : configQuery.isError ? (
        <ListPlaceholder
          description={t("pages.dnsServers.loadErrorDescription")}
          title={t("common.unableToLoadData")}
          variant="error"
        />
      ) : effectiveResolverIntegration(config) !== "dnsmasq" ? (
        <DnsManagementDisabled />
      ) : (
        <>
          {dnsServers.length === 0 ? (
            <ListPlaceholder
              description={t("pages.dnsServers.empty.description")}
              title={t("pages.dnsServers.empty.title")}
            />
          ) : (
            <div className="space-y-3">
              {serverSelection.isSelecting ? (
                <BulkSelectionToolbar
                  selection={serverSelection}
                  disabled={configMutationPending}
                  countLabel={t("pages.dnsServers.bulk.selected", {
                    count: serverSelection.selectedCount,
                  })}
                >
                  <Button
                    disabled={configMutationPending}
                    onClick={deleteServersBulk}
                    size="sm"
                    variant="destructive"
                  >
                    <Trash2 className="mr-1 h-4 w-4" />
                    {t("pages.dnsServers.bulk.delete", {
                      count: serverSelection.selectedCount,
                    })}
                  </Button>
                </BulkSelectionToolbar>
              ) : null}
              <DataTable
                mobileCards={{
                  titleColumns: [0],
                  bodyColumns: checkResponse ? [1, 2, 3] : [1, 2],
                }}
                headers={[
                  t("pages.dnsServers.headers.name"),
                  t("pages.dnsServers.headers.address"),
                  t("pages.dnsServers.headers.outbound"),
                  ...(checkResponse
                    ? [t("pages.dnsServers.headers.check")]
                    : []),
                  t("pages.dnsServers.headers.actions"),
                ]}
                rows={dnsServers.map((server) => [
                  <div className="font-medium" key={`${server.tag}-tag`}>
                    {server.tag}
                  </div>,
                  <span
                    className="text-sm text-muted-foreground"
                    key={`${server.tag}-address`}
                  >
                    {server.type === DnsServerType.keenetic
                      ? t("pages.dnsServers.keeneticAddress")
                      : server.address}
                    {server.ips && server.ips.length > 0 ? (
                      <span className="block text-xs">
                        {server.ips.join(", ")}
                      </span>
                    ) : null}
                  </span>,
                  <Badge
                    key={`${server.tag}-detour`}
                    variant={server.detour ? "outline" : "secondary"}
                  >
                    {server.detour || t("pages.dnsServers.none")}
                  </Badge>,
                  ...(checkResponse
                    ? [
                        <CheckStatusCell
                          key={`${server.tag}-check`}
                          response={checkResponse}
                          tag={server.tag}
                        />,
                      ]
                    : []),
                  <ActionButtons
                    mobileIcons
                    actions={[
                      {
                        disabled: configMutationPending,
                        icon: <Pencil className="h-4 w-4" />,
                        label: t("common.edit"),
                        onClick: () =>
                          navigate(
                            `/dns-servers/${encodeURIComponent(server.tag)}/edit`
                          ),
                      },
                      {
                        disabled: configMutationPending,
                        icon: <Trash2 className="h-4 w-4" />,
                        destructive: true,
                        label: t("common.delete"),
                        onClick: () => deleteServer(server.tag),
                      },
                    ]}
                    key={`${server.tag}-actions`}
                  />,
                ])}
                selection={{
                  isSelecting: serverSelection.isSelecting,
                  onStartSelecting: serverSelection.startSelecting,
                  rowIds: serverRowIds,
                  selectedIds: serverSelection.selectedIds,
                  disabled: configMutationPending,
                  onToggle: serverSelection.toggleOne,
                  onToggleAll: serverSelection.setAllVisible,
                  selectAllLabel: t("common.selection.selectAll"),
                  getRowLabel: (rowId) =>
                    t("common.selection.selectRow", { rowLabel: rowId }),
                }}
              />
            </div>
          )}

          <DnsServerLists
            bootstrap={config?.dns?.bootstrap ?? []}
            bootstrapOptions={bootstrapOptions}
            fallback={config?.dns?.fallback ?? []}
            onBootstrapChange={(tags) => saveServerList("bootstrap", tags)}
            onFallbackChange={(tags) => saveServerList("fallback", tags)}
            serverTags={serverRowIds}
          />
        </>
      )}
      <DeleteImpactDialog
        confirmLabel={t("pages.dnsServers.deleteDialog.confirm")}
        description={t("pages.dnsServers.deleteDialog.description", {
          tags: visibleDeleteRequest?.tags.join(", ") ?? "",
        })}
        impactItems={
          visibleDeleteRequest
            ? getDnsServerDeleteImpactItems(
                visibleDeleteRequest.config,
                visibleDeleteRequest.tags,
                visibleDeleteRequest.impact,
                t
              )
            : []
        }
        isPending={postConfigMutation.isPending}
        onConfirm={confirmDelete}
        onOpenChange={(open) => {
          if (!open && !postConfigMutation.isPending) {
            setDeleteRequest(null)
          }
        }}
        open={deleteRequest !== null}
        title={t("pages.dnsServers.deleteDialog.title")}
      />
    </div>
  )
}

function CheckStatusCell({
  response,
  tag,
}: {
  response: DnsCheckResponse
  tag: string
}) {
  const checked = response.servers.find((item) => item.tag === tag)
  return checked ? <DnsCheckBadge server={checked} /> : null
}

function getDnsServerDeleteImpactItems(
  config: ConfigObject | undefined,
  serverTags: string[],
  impact: DnsServerDeleteImpact,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  const items: DeleteImpactItem[] = []

  for (const tag of serverTags) {
    items.push({
      label: (
        <>
          {t("pages.dnsServers.deleteDialog.items.serverPrefix")}{" "}
          <strong>{tag}</strong>{" "}
          {t("pages.dnsServers.deleteDialog.items.serverSuffix")}
        </>
      ),
    })
  }

  for (const index of impact.matchingRuleIndexes) {
    items.push({
      label: t("pages.dnsServers.deleteDialog.items.dnsRule", {
        number: index + 1,
      }),
      details: getDnsRuleDetails(config?.dns?.rules?.[index], t),
    })
  }

  if (impact.usesFallback) {
    const fallback = config?.dns?.fallback ?? []
    items.push({
      label: t("pages.dnsServers.deleteDialog.items.fallback"),
      details: [
        formatDetail(
          t("pages.dnsServers.fallback.title"),
          <ChangeValue
            after={formatListValue(
              fallback.filter((tag) => !serverTags.includes(tag)),
              t
            )}
            before={formatListValue(fallback, t)}
          />
        ),
      ],
    })
  }

  if (impact.usesBootstrap) {
    const bootstrap = config?.dns?.bootstrap ?? []
    items.push({
      label: t("pages.dnsServers.deleteDialog.items.bootstrap"),
      details: [
        formatDetail(
          t("pages.dnsServers.bootstrap.title"),
          <ChangeValue
            after={formatListValue(
              bootstrap.filter((tag) => !serverTags.includes(tag)),
              t
            )}
            before={formatListValue(bootstrap, t)}
          />
        ),
      ],
    })
  }

  return items
}

function getDnsRuleDetails(
  rule: DnsRule | undefined,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  if (!rule) {
    return []
  }

  return [
    formatDetail(
      t("pages.dnsRules.criteriaLabels.lists"),
      formatListValue(rule.list, t)
    ),
    formatDetail(
      t("pages.dnsRules.headers.serverTag"),
      rule.servers.join(", ")
    ),
  ]
}

function formatDetail(label: string, value: ReactNode) {
  return (
    <>
      {label}: {value}
    </>
  )
}

function formatListValue(
  values: string[],
  t: (key: string, options?: Record<string, unknown>) => string
) {
  return values.length > 0 ? values.join(", ") : t("common.noneShort")
}

function ChangeValue({ after, before }: { after: string; before: string }) {
  return (
    <span className="inline-flex min-w-0 items-center gap-1 leading-4">
      <span className="min-w-0 truncate">{before}</span>
      <ArrowRight className="mt-px size-3 shrink-0" />
      <span className="min-w-0 truncate">{after}</span>
    </span>
  )
}

function getConfigData(response: getConfigResponse | undefined) {
  if (!response || response.status !== 200) {
    return undefined
  }

  return response.data.config
}
