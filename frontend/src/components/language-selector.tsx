import { useTranslation } from "react-i18next"

import { useLanguage } from "@/components/language-provider"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { LanguagesIcon } from "lucide-react"

const LANGUAGE_OPTIONS = [
  { value: "en", label: "English" },
  { value: "ru", label: "Русский" },
] as const

export function LanguageSelector() {
  const { language, setLanguage } = useLanguage()
  const { t } = useTranslation()
  const items = LANGUAGE_OPTIONS.map((option) => ({
    value: option.value,
    label: option.label,
  }))

  return (
    <div className="space-y-2">
      <p className="flex items-center gap-1.5 px-1 text-xs font-medium text-sidebar-foreground/70">
        <LanguagesIcon className="size-3.5" />
        {t("common.language")}
      </p>
      <Select
        items={items}
        onValueChange={(value) => value && setLanguage(value as "en" | "ru")}
        value={language}
      >
        <SelectTrigger aria-label={t("language.selectorAria")} className="bg-sidebar">
          <SelectValue placeholder="English" />
        </SelectTrigger>
        <SelectContent align="start" side="top" alignItemWithTrigger={false}>
          {LANGUAGE_OPTIONS.map((option) => (
            <SelectItem key={option.value} value={option.value}>
              {option.label}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    </div>
  )
}
