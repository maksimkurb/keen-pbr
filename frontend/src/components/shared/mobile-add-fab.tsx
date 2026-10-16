import { useEffect, useState } from "react"
import type { ReactNode } from "react"
import { useTranslation } from "react-i18next"

import { Button } from "@/components/ui/button"
import { getMobileFabCollapsedState } from "@/components/shared/mobile-add-fab-utils"

export function MobileAddFab({
  onClick,
  disabled,
  icon,
}: {
  onClick: () => void
  disabled?: boolean
  icon: ReactNode
}) {
  const { t } = useTranslation()
  const [collapsed, setCollapsed] = useState(false)

  useEffect(() => {
    let lastChangeY = window.scrollY
    const onScroll = () => {
      const currentY = window.scrollY
      const nextCollapsed = getMobileFabCollapsedState(currentY, lastChangeY)
      if (nextCollapsed === null) return
      setCollapsed(nextCollapsed)
      lastChangeY = currentY
    }

    window.addEventListener("scroll", onScroll, { passive: true })
    return () => window.removeEventListener("scroll", onScroll)
  }, [])

  return (
    <Button
      aria-label={t("common.add")}
      className={`fixed right-4 bottom-[calc(max(var(--warning-banner-height,0px),env(safe-area-inset-bottom))+1rem)] z-40 h-12 min-w-12 rounded-full shadow-lg transition-[width,padding,gap] duration-200 motion-reduce:transition-none md:hidden ${collapsed ? "gap-0 p-0" : "gap-2 px-4"}`}
      disabled={disabled}
      onClick={onClick}
      size="default"
    >
      {icon}
      <span
        aria-hidden="true"
        className={`overflow-hidden whitespace-nowrap transition-[max-width,opacity] duration-200 motion-reduce:transition-none ${collapsed ? "max-w-0 opacity-0" : "max-w-40 opacity-100"}`}
      >
        {t("common.add")}
      </span>
    </Button>
  )
}
