import { useTranslation } from 'react-i18next';

export default function GeneralSettings() {
  const { t } = useTranslation();

  return (
    <div>
      <h1 className="text-3xl font-bold">{t('settings.title')}</h1>
      <p className="text-muted-foreground mt-2">
        Manage global configuration settings
      </p>
    </div>
  );
}
