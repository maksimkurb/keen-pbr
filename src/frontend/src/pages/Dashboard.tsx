import { useTranslation } from 'react-i18next';

export default function Dashboard() {
  const { t } = useTranslation();

  return (
    <div>
      <h1 className="text-3xl font-bold">{t('dashboard.title')}</h1>
      <p className="text-muted-foreground mt-2">
        System status and domain routing checker
      </p>
    </div>
  );
}
