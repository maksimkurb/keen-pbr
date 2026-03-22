import i18n from "i18next"
import { initReactI18next } from "react-i18next"

const resources = {
  en: {
    translation: {
      common: {
        language: "Language",
        theme: "Theme",
      },
      language: {
        selectorAria: "Language selector",
        english: "English",
        russian: "Russian",
      },
      theme: {
        selectorAria: "Theme selector",
        useSystem: "Use system setting",
        light: "Light",
        dark: "Dark",
      },
      nav: {
        groups: {
          general: "General",
          internet: "Internet",
          networkRules: "Network Rules",
        },
        items: {
          systemMonitor: "System monitor",
          settings: "Settings",
          outbounds: "Outbounds",
          dnsServers: "DNS Servers",
          lists: "Lists",
          routingRules: "Routing rules",
          dnsRules: "DNS Rules",
        },
      },
      brand: {
        logoAlt: "keen-pbr logo",
        tagline: "Get packets sorted",
        openMenu: "Open menu",
      },
      warning: {
        compact: {
          draftPending: "Configuration draft pending save.",
          saving: "Saving...",
          apply: "Apply",
          resolverStale: "dnsmasq config is stale; reload required.",
          reloading: "Reloading...",
          reload: "Reload",
        },
        full: {
          unsavedTitle: "Configuration is unsaved",
          unsavedDescription:
            "Configuration has been staged in memory. Save and apply it to persist it to disk and reload the service.",
          applying: "Applying...",
          applyConfig: "Apply config",
          staleTitle: "dnsmasq is using a stale resolver config",
          staleDescription:
            "The expected resolver hash ({{expected}}…) doesn't match dnsmasq's active hash ({{actual}}…). Reload keen-pbr to regenerate and apply the current resolver configuration.",
          reloading: "Reloading...",
          reloadService: "Reload service",
        },
      },
    },
  },
  ru: {
    translation: {
      common: {
        language: "Язык",
        theme: "Тема",
      },
      language: {
        selectorAria: "Выбор языка",
        english: "Английский",
        russian: "Русский",
      },
      theme: {
        selectorAria: "Выбор темы",
        useSystem: "Использовать системную",
        light: "Светлая",
        dark: "Тёмная",
      },
      nav: {
        groups: {
          general: "Общее",
          internet: "Интернет",
          networkRules: "Сетевые правила",
        },
        items: {
          systemMonitor: "Мониторинг системы",
          settings: "Настройки",
          outbounds: "Выходы",
          dnsServers: "DNS-серверы",
          lists: "Списки",
          routingRules: "Правила маршрутизации",
          dnsRules: "DNS-правила",
        },
      },
      brand: {
        logoAlt: "логотип keen-pbr",
        tagline: "Сортировка пакетов",
        openMenu: "Открыть меню",
      },
      warning: {
        compact: {
          draftPending: "Есть черновик конфигурации, ожидающий сохранения.",
          saving: "Сохранение...",
          apply: "Применить",
          resolverStale: "Конфиг dnsmasq устарел; требуется перезагрузка.",
          reloading: "Перезагрузка...",
          reload: "Перезагрузить",
        },
        full: {
          unsavedTitle: "Конфигурация не сохранена",
          unsavedDescription:
            "Конфигурация загружена в память. Сохраните и примените её, чтобы записать на диск и перезагрузить сервис.",
          applying: "Применение...",
          applyConfig: "Применить конфиг",
          staleTitle: "dnsmasq использует устаревший конфиг резолвера",
          staleDescription:
            "Ожидаемый хеш резолвера ({{expected}}…) не совпадает с активным хешем dnsmasq ({{actual}}…). Перезагрузите keen-pbr, чтобы пересобрать и применить текущую конфигурацию резолвера.",
          reloading: "Перезагрузка...",
          reloadService: "Перезагрузить сервис",
        },
      },
    },
  },
} as const

function detectInitialLanguage() {
  if (typeof navigator === "undefined") {
    return "en"
  }

  const preferred = navigator.languages?.[0] ?? navigator.language
  return preferred.toLowerCase().startsWith("ru") ? "ru" : "en"
}

void i18n.use(initReactI18next).init({
  resources,
  lng: detectInitialLanguage(),
  fallbackLng: "en",
  interpolation: { escapeValue: false },
})

export default i18n
