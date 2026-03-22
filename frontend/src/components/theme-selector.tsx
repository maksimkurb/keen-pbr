import { useTranslation } from "react-i18next"

import { useTheme } from "@/components/theme-provider"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

const THEME_OPTIONS = [
  { value: "system", labelKey: "theme.useSystem" },
  { value: "light", labelKey: "theme.light" },
  { value: "dark", labelKey: "theme.dark" },
] as const

export function ThemeSelector() {
  const { theme, setTheme } = useTheme()
  const { t } = useTranslation()

  return (
    <div className="space-y-2">
      <p className="px-1 text-xs font-medium text-sidebar-foreground/70">{t("common.theme")}</p>
      <Select
        defaultValue={theme}
        onValueChange={(value) => setTheme(value as "system" | "light" | "dark")}
        value={theme}
      >
        <SelectTrigger aria-label={t("theme.selectorAria")} className="bg-sidebar">
          <SelectValue placeholder={t("theme.useSystem")} />
        </SelectTrigger>
        <SelectContent align="start">
          {THEME_OPTIONS.map((option) => (
            <SelectItem key={option.value} value={option.value}>
              {t(option.labelKey)}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    </div>
  )
}
