import { useTranslation } from 'react-i18next';

export default function Lists() {
  const { t } = useTranslation();

  return (
    <div>
      <h1 className="text-3xl font-bold">{t('lists.title')}</h1>
      <p className="text-muted-foreground mt-2">
        Manage IP/domain lists
      </p>
    </div>
  );
}
