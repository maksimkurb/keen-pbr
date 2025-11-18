import { useTranslation } from 'react-i18next';

export default function RoutingRules() {
  const { t } = useTranslation();

  return (
    <div>
      <h1 className="text-3xl font-bold">{t('routingRules.title')}</h1>
      <p className="text-muted-foreground mt-2">
        Manage IPSet routing configurations
      </p>
    </div>
  );
}
