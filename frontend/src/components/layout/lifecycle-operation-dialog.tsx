import { useEffect, useState } from "react"
import { CheckIcon, CircleIcon, LoaderCircleIcon, XIcon } from "lucide-react"

import { useGetHealthService } from "@/api/generated/keen-api"
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from "@/components/ui/dialog"
import { cn } from "@/lib/utils"

export function LifecycleOperationDialog() {
  const health = useGetHealthService()
  const operation = health.data?.data.lifecycle_operation
  const dismissedKey = operation ? `keen-pbr.lifecycle.dismissed.${operation.id}` : ""
  const [dismissed, setDismissed] = useState(false)

  useEffect(() => {
    setDismissed(Boolean(dismissedKey && localStorage.getItem(dismissedKey)))
  }, [dismissedKey])

  useEffect(() => {
    if (operation?.status === "succeeded") {
      const timer = window.setTimeout(() => setDismissed(true), 1200)
      return () => window.clearTimeout(timer)
    }
  }, [operation?.id, operation?.status])

  if (!operation) return null
  const running = operation.status === "running"
  const open = running || (operation.status === "failed" && !dismissed)
  return (
    <Dialog
      open={open}
      onOpenChange={(next) => {
        if (!next && !running) {
          localStorage.setItem(dismissedKey, "1")
          setDismissed(true)
        }
      }}
    >
      <DialogContent showCloseButton={!running}>
        <DialogHeader>
          <DialogTitle>Runtime operation</DialogTitle>
          <DialogDescription>{operation.type.replace("_", " ")}</DialogDescription>
        </DialogHeader>
        <ol className="grid gap-3">
          {operation.stages.map((stage) => (
            <li className="flex gap-3" key={stage.id}>
              <StageIcon status={stage.status} />
              <div className="min-w-0">
                <p className="font-medium">{stage.title}</p>
                {stage.detail ? <p className="text-muted-foreground">{stage.detail}</p> : null}
              </div>
            </li>
          ))}
        </ol>
        {operation.error ? <p className="rounded-md bg-destructive/10 p-3 text-destructive">{operation.error}</p> : null}
      </DialogContent>
    </Dialog>
  )
}

function StageIcon({ status }: { status: string }) {
  const className = "mt-0.5 size-5 shrink-0"
  if (status === "running") return <LoaderCircleIcon className={cn(className, "animate-spin text-primary")} />
  if (status === "succeeded") return <CheckIcon className={cn(className, "rounded-full bg-emerald-600 p-0.5 text-white")} />
  if (status === "failed") return <XIcon className={cn(className, "rounded-full bg-destructive p-0.5 text-destructive-foreground")} />
  return <CircleIcon className={cn(className, "text-muted-foreground")} />
}
