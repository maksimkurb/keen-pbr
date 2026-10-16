import { Fragment, type ReactNode, useState } from "react"
import { useTranslation } from "react-i18next"
import { ArrowDown, ArrowUp, Layers, Plus, Trash2, X } from "lucide-react"

import type {
  RuntimeInterfaceState,
  RuntimeOutboundState,
} from "@/api/generated/model"
import { IconButtonWithTooltip } from "@/components/shared/icon-button-with-tooltip"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from "@/components/ui/popover"
import { cn } from "@/lib/utils"

export type LadderCandidate = {
  tag: string
  /** Interface name, or `table N` for table outbounds. */
  detail?: string
  runtime?: RuntimeOutboundState
}

type BadgeVariant = "success" | "warning" | "destructive" | "outline"

export function GroupLadder({
  steps,
  candidates,
  memberRuntime,
  strategy,
  stepErrors,
  onChange,
  renderTarget,
  renderWeight,
  showWeights = false,
}: {
  steps: string[][]
  candidates: LadderCandidate[]
  /** Runtime state of each member inside this group, by outbound tag. */
  memberRuntime: Map<string, RuntimeInterfaceState>
  strategy: "priority" | "balance"
  stepErrors: Array<string | null>
  onChange: (steps: string[][]) => void
  renderTarget?: (stepIndex: number, memberIndex: number) => ReactNode
  /** Per-member weight editor, rendered when `showWeights`. */
  renderWeight?: (stepIndex: number, memberIndex: number) => ReactNode
  showWeights?: boolean
}) {
  const { t } = useTranslation()
  const candidateByTag = new Map(candidates.map((item) => [item.tag, item]))
  const used = new Set(steps.flat())
  const available = candidates.filter((item) => !used.has(item.tag))
  const activeStep = steps.findIndex((step) =>
    step.some((tag) => memberRuntime.get(tag)?.status === "active")
  )

  const update = (index: number, members: string[]) =>
    onChange(steps.map((step, i) => (i === index ? members : step)))
  const move = (from: number, to: number) => {
    const next = [...steps]
    const [moved] = next.splice(from, 1)
    next.splice(to, 0, moved)
    onChange(next)
  }

  return (
    <div className="space-y-4">
      <div className="flex items-start gap-2.5 rounded-lg bg-primary/5 px-3 py-2.5 text-sm text-primary">
        <Layers className="mt-0.5 size-4 shrink-0" />
        <span>{describeLadder(steps.length, strategy, t)}</span>
      </div>

      <div className="flex flex-col">
        {steps.map((members, stepIndex) => {
          const isActive = stepIndex === activeStep
          const error = stepErrors[stepIndex]
          return (
            <Fragment key={stepIndex}>
              {stepIndex > 0 ? (
                <div className="flex items-center gap-2 py-1.5 pl-5 text-xs text-muted-foreground">
                  <ArrowDown className="size-4" />
                  {t("pages.outboundUpsert.ladder.ifAllDown")}
                </div>
              ) : null}
              <div
                className={cn(
                  "rounded-xl border bg-card px-3.5 py-3",
                  isActive && "border-success/50"
                )}
              >
                <div className="mb-3 flex flex-wrap items-center gap-2">
                  <span className="inline-flex size-5.5 shrink-0 items-center justify-center rounded-full bg-primary text-xs font-semibold text-primary-foreground">
                    {stepIndex + 1}
                  </span>
                  <span className="text-sm font-medium">
                    {stepIndex === 0
                      ? t("pages.outboundUpsert.ladder.primaryStep")
                      : t("pages.outboundUpsert.ladder.backupStep", {
                          index: stepIndex + 1,
                        })}
                  </span>
                  {isActive ? (
                    <Badge size="xs" variant="success">
                      {t("pages.outboundUpsert.ladder.activeNow")}
                    </Badge>
                  ) : null}
                  <div className="ml-auto flex gap-1">
                    <IconButtonWithTooltip
                      disabled={stepIndex === 0}
                      label={t("pages.outboundUpsert.ladder.moveUp")}
                      onClick={() => move(stepIndex, stepIndex - 1)}
                      size="icon-xs"
                      type="button"
                      variant="ghost"
                    >
                      <ArrowUp />
                    </IconButtonWithTooltip>
                    <IconButtonWithTooltip
                      disabled={stepIndex === steps.length - 1}
                      label={t("pages.outboundUpsert.ladder.moveDown")}
                      onClick={() => move(stepIndex, stepIndex + 1)}
                      size="icon-xs"
                      type="button"
                      variant="ghost"
                    >
                      <ArrowDown />
                    </IconButtonWithTooltip>
                    <IconButtonWithTooltip
                      disabled={steps.length === 1}
                      label={t("pages.outboundUpsert.ladder.removeStep")}
                      onClick={() =>
                        onChange(steps.filter((_, i) => i !== stepIndex))
                      }
                      size="icon-xs"
                      type="button"
                      variant="ghost"
                    >
                      <Trash2 />
                    </IconButtonWithTooltip>
                  </div>
                </div>

                <div className="flex flex-col items-stretch gap-2">
                  {members.map((tag, memberIndex) => (
                    <MemberChip
                      candidate={candidateByTag.get(tag)}
                      key={tag}
                      onRemove={() =>
                        update(
                          stepIndex,
                          members.filter((_, i) => i !== memberIndex)
                        )
                      }
                      role={getRole(
                        memberRuntime.get(tag),
                        isActive,
                        strategy,
                        t
                      )}
                      runtime={memberRuntime.get(tag)}
                      tag={tag}
                      target={renderTarget?.(stepIndex, memberIndex)}
                      weight={
                        showWeights
                          ? renderWeight?.(stepIndex, memberIndex)
                          : undefined
                      }
                    />
                  ))}
                  <AddMemberButton
                    available={available}
                    onPick={(tag) => update(stepIndex, [...members, tag])}
                  />
                </div>
                {error ? (
                  <p className="mt-2 text-xs text-destructive">{error}</p>
                ) : null}
              </div>
            </Fragment>
          )
        })}
      </div>

      <Button
        onClick={() => onChange([...steps, []])}
        type="button"
        variant="outline"
      >
        <Plus />
        {t("pages.outboundUpsert.ladder.addStep")}
      </Button>
    </div>
  )
}

function MemberChip({
  tag,
  candidate,
  runtime,
  role,
  target,
  weight,
  onRemove,
}: {
  tag: string
  candidate?: LadderCandidate
  runtime?: RuntimeInterfaceState
  role?: { label: string; variant: BadgeVariant }
  target?: ReactNode
  weight?: ReactNode
  onRemove: () => void
}) {
  const { t } = useTranslation()
  return (
    <div
      className="flex min-h-9 min-w-0 flex-wrap items-center gap-x-2 gap-y-1 rounded-lg border bg-card py-1 pr-1 pl-2.5"
      title={runtime?.detail ?? candidate?.runtime?.detail}
    >
      <AvailabilityDot runtime={candidate?.runtime} member={runtime} />
      <span className="font-medium">{tag}</span>
      {candidate?.detail ? (
        <span className="font-mono text-xs text-muted-foreground">
          · {candidate.detail}
        </span>
      ) : null}
      {typeof runtime?.latency_ms === "number" ? (
        <span className="text-xs text-muted-foreground">
          {t("pages.outboundUpsert.ladder.latency", {
            value: runtime.latency_ms,
          })}
        </span>
      ) : null}
      {role ? (
        <Badge size="xs" variant={role.variant}>
          {role.label}
        </Badge>
      ) : null}
      {target || weight ? (
        <div className="ml-auto flex flex-wrap items-center gap-x-4 gap-y-1">
          {weight}
          {target}
        </div>
      ) : null}
      <IconButtonWithTooltip
        className={cn(!target && !weight && "ml-auto")}
        label={t("pages.outboundUpsert.ladder.removeMember", { tag })}
        onClick={onRemove}
        size="icon-xs"
        type="button"
        variant="ghost"
      >
        <X />
      </IconButtonWithTooltip>
    </div>
  )
}

function AddMemberButton({
  available,
  onPick,
}: {
  available: LadderCandidate[]
  onPick: (tag: string) => void
}) {
  const { t } = useTranslation()
  const [open, setOpen] = useState(false)

  return (
    <Popover onOpenChange={setOpen} open={open}>
      <PopoverTrigger
        render={
          <Button
            className="self-start"
            size="sm"
            type="button"
            variant="outline"
          />
        }
      >
        <Plus />
        {t("pages.outboundUpsert.ladder.add")}
      </PopoverTrigger>
      <PopoverContent align="start" className="w-80 gap-0 p-1">
        {available.length === 0 ? (
          <p className="px-2.5 py-2 text-sm text-muted-foreground">
            {t("pages.outboundUpsert.ladder.noOptions")}
          </p>
        ) : (
          available.map((item) => (
            <button
              className="flex w-full cursor-pointer items-center gap-2 rounded-md px-2.5 py-1.5 text-left text-sm outline-none hover:bg-muted focus-visible:bg-muted"
              key={item.tag}
              onClick={() => {
                onPick(item.tag)
                setOpen(false)
              }}
              type="button"
            >
              <AvailabilityDot runtime={item.runtime} />
              <span className="font-medium">{item.tag}</span>
              {item.detail ? (
                <span className="truncate font-mono text-xs text-muted-foreground">
                  · {item.detail}
                </span>
              ) : null}
              {item.runtime ? (
                <span className="ml-auto shrink-0 text-xs text-muted-foreground">
                  {t(`runtime.outboundStatus.${item.runtime.status}`)}
                </span>
              ) : null}
            </button>
          ))
        )}
      </PopoverContent>
    </Popover>
  )
}

function AvailabilityDot({
  runtime,
  member,
}: {
  runtime?: RuntimeOutboundState
  member?: RuntimeInterfaceState
}) {
  const status = member?.status ?? runtime?.status
  const tone =
    status === "active" || status === "backup" || status === "healthy"
      ? "bg-success"
      : status === "degraded"
        ? "bg-warning"
        : status === "unavailable"
          ? "bg-destructive"
          : "bg-muted-foreground/40"
  return (
    <span aria-hidden className={cn("size-2 shrink-0 rounded-full", tone)} />
  )
}

function getRole(
  runtime: RuntimeInterfaceState | undefined,
  stepActive: boolean,
  strategy: "priority" | "balance",
  t: (key: string) => string
): { label: string; variant: BadgeVariant } | undefined {
  const role = (key: string, variant: BadgeVariant) => ({
    label: t(`pages.outboundUpsert.ladder.roles.${key}`),
    variant,
  })
  switch (runtime?.status) {
    case "active":
      return role(strategy === "balance" ? "balanced" : "selected", "success")
    case "backup":
      return stepActive
        ? role("standby", "outline")
        : role("waiting", "outline")
    case "degraded":
      return role("degraded", "warning")
    case "unavailable":
      return role("unavailable", "destructive")
    default:
      return undefined
  }
}

function describeLadder(
  stepCount: number,
  strategy: "priority" | "balance",
  t: (key: string, options?: Record<string, unknown>) => string
) {
  const head = t(`pages.outboundUpsert.ladder.phrase.${strategy}`)
  if (stepCount <= 1) {
    return head
  }
  return `${head} ${
    stepCount === 2
      ? t("pages.outboundUpsert.ladder.phrase.fallbackOne")
      : t("pages.outboundUpsert.ladder.phrase.fallbackMany", {
          last: stepCount,
        })
  }`
}
