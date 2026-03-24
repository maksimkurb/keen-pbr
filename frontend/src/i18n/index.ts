import i18n from "i18next"
import { initReactI18next } from "react-i18next"

import { enTranslation } from "./en"
import { ruTranslation } from "./ru"

export type Language = "en" | "ru"

export const DEFAULT_LANGUAGE: Language = "en"
export const LANGUAGE_STORAGE_KEY = "language"

const LANGUAGE_VALUES: Language[] = ["en", "ru"]

const resources = {
  en: {
    translation: enTranslation,
  },
  ru: {
    translation: ruTranslation,
  },
} as const

export function isLanguage(value: string | null): value is Language {
  if (value === null) {
    return false
  }

  return LANGUAGE_VALUES.includes(value as Language)
}

function detectInitialLanguage(): Language {
  if (typeof window !== "undefined") {
    const storedLanguage = window.localStorage.getItem(LANGUAGE_STORAGE_KEY)
    if (isLanguage(storedLanguage)) {
      return storedLanguage
    }
  }

  if (typeof navigator === "undefined") {
    return DEFAULT_LANGUAGE
  }

  const preferred = navigator.languages?.[0] ?? navigator.language
  return preferred.toLowerCase().startsWith("ru") ? "ru" : DEFAULT_LANGUAGE
}

void i18n.use(initReactI18next).init({
  resources,
  lng: detectInitialLanguage(),
  fallbackLng: DEFAULT_LANGUAGE,
  interpolation: { escapeValue: false },
})

export default i18n
