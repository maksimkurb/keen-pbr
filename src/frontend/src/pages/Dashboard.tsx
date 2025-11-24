import { KeeneticWidget } from '../../components/dashboard/KeeneticWidget';
import { KeenPbrWidget } from '../../components/dashboard/KeenPbrWidget';
import { DNSCheckWidget } from '../../components/dashboard/DNSCheckWidget';
import { DomainCheckerWidget } from '../../components/dashboard/DomainCheckerWidget';
import { SelfCheckWidget } from '../../components/dashboard/SelfCheckWidget';

export default function Dashboard() {
  return (
    <div className="space-y-6">
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
