import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import en from './locales/en.json';
import ru from './locales/ru.json';

// Detect browser language
const getBrowserLanguage = (): string => {
  const browserLang = navigator.language || (navigator as any).userLanguage;
  // Extract the language code (e.g., 'en' from 'en-US', 'ru' from 'ru-RU')
  const langCode = browserLang.split('-')[0];
  // Check if we support this language, otherwise fallback to English
  return ['en', 'ru'].includes(langCode) ? langCode : 'en';
};

// Try to get language from localStorage, otherwise use browser language
const getInitialLanguage = (): string => {
  const storedLang = localStorage.getItem('i18nextLng');
  return storedLang || getBrowserLanguage();
};

i18n
  .use(initReactI18next)
  .init({
    resources: {
      en: { translation: en },
      ru: { translation: ru }
    },
    lng: getInitialLanguage(),
    fallbackLng: 'en',
    interpolation: {
      escapeValue: false
    }
  });

// Save language preference to localStorage when it changes
i18n.on('languageChanged', (lng) => {
  localStorage.setItem('i18nextLng', lng);
});

export default i18n;
