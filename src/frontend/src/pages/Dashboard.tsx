import { useTranslation } from 'react-i18next';
import { ServiceStatusWidget } from '../../components/dashboard/ServiceStatusWidget';
import { DomainCheckerWidget } from '../../components/dashboard/DomainCheckerWidget';

export default function Dashboard() {
  const { t } = useTranslation();

  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-3xl font-bold">{t('dashboard.title')}</h1>
        <p className="text-muted-foreground mt-2">
          System status and domain routing checker
        </p>
      </div>

      <ServiceStatusWidget />

      <DomainCheckerWidget />
    </div>
  );
}
