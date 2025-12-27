import { LanguagesIcon } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '../ui/select';

interface LanguageSelectorProps {
  className?: string;
}

const languages = ['en', 'ru'];

export function LanguageSelector({ className }: LanguageSelectorProps) {
  const { t, i18n } = useTranslation();

  const changeLanguage = (lng: string) => {
    i18n.changeLanguage(lng);
  };

  return (
    <Select value={i18n.language} onValueChange={changeLanguage}>
      <SelectTrigger className={className}>
        <div className="flex items-center gap-2">
          <LanguagesIcon className="h-4 w-4" />
          <SelectValue />
        </div>
      </SelectTrigger>
      <SelectContent>
        {languages.map((lang) => (
          <SelectItem key={lang} value={lang}>
            {t(`common.language.${lang}`)}
          </SelectItem>
        ))}
      </SelectContent>
    </Select>
  );
}
