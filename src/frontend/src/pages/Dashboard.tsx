import { useTranslation } from 'react-i18next';
import { ServiceStatusWidget } from '../../components/dashboard/ServiceStatusWidget';
import { DNSCheckWidget } from '../../components/dashboard/DNSCheckWidget';
import { DomainCheckerWidget } from '../../components/dashboard/DomainCheckerWidget';
import { SelfCheckWidget } from '../../components/dashboard/SelfCheckWidget';

export default function Dashboard() {
  const { t } = useTranslation();

  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-3xl font-bold">{t('dashboard.title')}</h1>
        <p className="text-muted-foreground mt-2">
          {t('dashboard.description')}
        </p>
      </div>

      <ServiceStatusWidget />

      <DNSCheckWidget />

      <SelfCheckWidget />

      <DomainCheckerWidget />
    </div>
  );
}
