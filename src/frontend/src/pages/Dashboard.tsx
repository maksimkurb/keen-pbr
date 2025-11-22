import { useTranslation } from 'react-i18next';
import { KeeneticWidget } from '../../components/dashboard/KeeneticWidget';
import { KeenPbrWidget } from '../../components/dashboard/KeenPbrWidget';
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

      {/* System Status - Plain flex layout without card wrapper */}
      <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-4">
        <KeeneticWidget />
        <KeenPbrWidget />
        <DNSCheckWidget />
      </div>

      <SelfCheckWidget />

      <DomainCheckerWidget />
    </div>
  );
}
