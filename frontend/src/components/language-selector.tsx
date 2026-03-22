import { useTranslation } from "react-i18next"

import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

const LANGUAGE_OPTIONS = [
  { value: "en", labelKey: "language.english" },
  { value: "ru", labelKey: "language.russian" },
] as const

export function LanguageSelector() {
  const { i18n, t } = useTranslation()

  return (
    <div className="space-y-2">
      <p className="px-1 text-xs font-medium text-sidebar-foreground/70">
        {t("common.language")}
      </p>
      <Select
        onValueChange={(value) => value && void i18n.changeLanguage(value)}
        value={i18n.resolvedLanguage ?? "en"}
      >
        <SelectTrigger aria-label={t("language.selectorAria")} className="bg-sidebar">
          <SelectValue placeholder={t("language.english")} />
        </SelectTrigger>
        <SelectContent align="start">
          {LANGUAGE_OPTIONS.map((option) => (
            <SelectItem key={option.value} value={option.value}>
              {t(option.labelKey)}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    </div>
  )
}
