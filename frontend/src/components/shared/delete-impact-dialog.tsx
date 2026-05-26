import { AlertTriangle } from "lucide-react"
import type { ReactNode } from "react"
import { useTranslation } from "react-i18next"

import { Button } from "@/components/ui/button"
import {
  Dialog,
  DialogClose,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"

export type DeleteImpactItem = {
  details?: ReactNode[]
  label: ReactNode
}

type DeleteImpactDialogProps = {
  confirmLabel: string
  description: string
  impactItems: DeleteImpactItem[]
  isPending?: boolean
  onConfirm: () => void
  onOpenChange: (open: boolean) => void
  open: boolean
  title: string
}

export function DeleteImpactDialog({
  confirmLabel,
  description,
  impactItems,
  isPending = false,
  onConfirm,
  onOpenChange,
  open,
  title,
}: DeleteImpactDialogProps) {
  const { t } = useTranslation()

  return (
    <Dialog onOpenChange={onOpenChange} open={open}>
      <DialogContent className="sm:max-w-lg">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <AlertTriangle className="size-4 text-destructive" />
            {title}
          </DialogTitle>
          <DialogDescription>{description}</DialogDescription>
        </DialogHeader>

        {impactItems.length > 0 ? (
          <div className="max-h-72 overflow-y-auto rounded-lg border bg-muted/30 p-3">
            <ul className="space-y-2 text-sm leading-5">
              {impactItems.map((item, index) => (
                <li className="flex gap-2" key={index}>
                  <span className="mt-[0.45rem] size-1.5 shrink-0 rounded-full bg-muted-foreground" />
                  <div className="min-w-0 space-y-0.5">
                    <div>{item.label}</div>
                    {item.details && item.details.length > 0 ? (
                      <div className="space-y-0.5 border-l border-border pl-3 text-xs leading-4 text-muted-foreground">
                        {item.details.map((detail, detailIndex) => (
                          <div key={detailIndex}>{detail}</div>
                        ))}
                      </div>
                    ) : null}
                  </div>
                </li>
              ))}
            </ul>
          </div>
        ) : null}

        <DialogFooter>
          <DialogClose
            render={<Button disabled={isPending} variant="outline" />}
          >
            {t("common.cancel")}
          </DialogClose>
          <Button
            disabled={isPending}
            onClick={onConfirm}
            variant="destructive"
          >
            {confirmLabel}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}
