import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { useSettings, useUpdateSettings } from '../../src/hooks/useSettings';
import {
  Card,
  CardHeader,
  CardTitle,
  CardDescription,
  CardContent,
} from '../ui/card';
import {
  Field,
  FieldLabel,
  FieldDescription,
  FieldGroup,
  FieldSeparator,
} from '../ui/field';
import { Input } from '../ui/input';
import { Checkbox } from '../ui/checkbox';
import { BaseSettingsForm } from '../ui/base-settings-form';
import { DNSServerSettings } from './DNSServerSettings';
import type { GeneralConfig } from '../../src/api/generated-types';

export function SettingsForm() {
  const { t } = useTranslation();
  const { data: settings, isLoading, error } = useSettings();
  const updateSettings = useUpdateSettings();

  const defaultData: GeneralConfig = {
    lists_output_dir: '',
    interface_monitoring_interval_seconds: 0,
    auto_update_lists: {
      enabled: true,
      interval_hours: 24,
    },
    dns_server: {
      enable: true,
      listen_addr: '[::]',
      listen_port: 15353,
      upstreams: ['keenetic://'],
      cache_max_domains: 1000,
      drop_aaaa: true,
      ipset_entry_additional_ttl_sec: 7200,
      listed_domains_dns_cache_ttl_sec: 30,
      remap_53_interfaces: ['br0', 'br1'],
    },
  };

  const handleSave = async (formData: GeneralConfig) => {
    try {
      await updateSettings.mutateAsync(formData);
      toast.success(t('common.success'), {
        description: t('settings.saveSuccess'),
      });
    } catch (error) {
      toast.error(t('common.error'), {
        description:
          error instanceof Error ? error.message : t('settings.saveError'),
      });
      throw error; // Re-throw to prevent form state update
    }
  };

  return (
    <BaseSettingsForm
      data={settings}
      isLoading={isLoading}
      error={error}
      isSaving={updateSettings.isPending}
      onSave={handleSave}
      defaultData={defaultData}
    >
      {(formData, setFormData) => (
        <>
          {/* Lists Settings */}
          <Card>
            <CardHeader>
              <CardTitle>{t('settings.listsTitle')}</CardTitle>
              <CardDescription>
                {t('settings.listsDescription')}
              </CardDescription>
            </CardHeader>

            <CardContent>
              <FieldGroup>
                {/* Lists Output Directory */}
                <Field>
                  <FieldLabel htmlFor="lists_output_dir">
                    {t('settings.listsOutputDir')}
                  </FieldLabel>
                  <FieldDescription>
                    {t('settings.listsOutputDirDescription')}
                  </FieldDescription>
                  <Input
                    id="lists_output_dir"
                    type="text"
                    value={formData.lists_output_dir}
                    onChange={(e) =>
                      setFormData((prev) => ({
                        ...prev,
                        lists_output_dir: e.target.value,
                      }))
                    }
                    placeholder="/opt/etc/keen-pbr/lists.d"
                    required
                  />
                </Field>

                <FieldSeparator />

                {/* Auto-update Lists */}
                <Field orientation="horizontal">
                  <div className="flex items-center space-x-3">
                    <Checkbox
                      id="auto_update_enabled"
                      checked={formData.auto_update_lists?.enabled ?? false}
                      onCheckedChange={(checked) =>
                        setFormData((prev) => ({
                          ...prev,
                          auto_update_lists: {
                            ...(prev.auto_update_lists ?? { enabled: false, interval_hours: 24 }),
                            enabled: !!checked,
                          },
                        }))
                      }
                    />
                    <FieldLabel
                      htmlFor="auto_update_enabled"
                      className="cursor-pointer flex flex-col items-start gap-0"
                    >
                      {t('settings.autoUpdateLists')}
                      <FieldDescription>
                        {t('settings.autoUpdateListsDescription')}
                      </FieldDescription>
                    </FieldLabel>
                  </div>
                </Field>

                {/* Update Interval */}
                <Field>
                  <FieldLabel htmlFor="update_interval_hours">
                    {t('settings.updateIntervalHours')}
                  </FieldLabel>
                  <FieldDescription>
                    {t('settings.updateIntervalHoursDescription')}
                  </FieldDescription>
                  <Input
                    id="update_interval_hours"
                    type="number"
                    min="1"
                    value={formData.auto_update_lists?.interval_hours ?? 24}
                    onChange={(e) => {
                      const value = parseInt(e.target.value, 10) || 1;
                      setFormData((prev) => ({
                        ...prev,
                        auto_update_lists: {
                          ...(prev.auto_update_lists ?? { enabled: false, interval_hours: 24 }),
                          interval_hours: value,
                        },
                      }));
                    }}
                    placeholder="24"
                    disabled={!formData.auto_update_lists?.enabled}
                  />
                </Field>
              </FieldGroup>
            </CardContent>
          </Card>

          {/* Interfaces Settings */}
          <Card>
            <CardHeader>
              <CardTitle>{t('settings.interfacesTitle')}</CardTitle>
              <CardDescription>
                {t('settings.interfacesDescription')}
              </CardDescription>
            </CardHeader>

            <CardContent>
              <FieldGroup>
                {/* Enable Interface Monitoring */}
                <Field orientation="horizontal">
                  <div className="flex items-center space-x-3">
                    <Checkbox
                      id="enable_interface_monitoring"
                      checked={
                        formData.interface_monitoring_interval_seconds > 0
                      }
                      onCheckedChange={(checked) =>
                        setFormData((prev) => ({
                          ...prev,
                          interface_monitoring_interval_seconds: checked
                            ? 10
                            : 0,
                        }))
                      }
                    />
                    <FieldLabel
                      htmlFor="enable_interface_monitoring"
                      className="cursor-pointer flex flex-col items-start gap-0"
                    >
                      {t('settings.enableInterfaceMonitoring')}
                      <FieldDescription>
                        {t('settings.enableInterfaceMonitoringDescription')}
                      </FieldDescription>
                    </FieldLabel>
                  </div>
                </Field>

                {/* Interface Monitoring Interval */}
                <Field>
                  <FieldLabel htmlFor="interface_monitoring_interval_seconds">
                    {t('settings.interfaceMonitoringInterval')}
                  </FieldLabel>
                  <FieldDescription>
                    {t('settings.interfaceMonitoringIntervalDescription')}
                  </FieldDescription>
                  <Input
                    id="interface_monitoring_interval_seconds"
                    type="number"
                    min="10"
                    step="1"
                    value={formData.interface_monitoring_interval_seconds}
                    onChange={(e) => {
                      const value = parseInt(e.target.value, 10) || 10;
                      setFormData((prev) => ({
                        ...prev,
                        interface_monitoring_interval_seconds: Math.max(
                          10,
                          value,
                        ),
                      }));
                    }}
                    placeholder="10"
                    disabled={
                      formData.interface_monitoring_interval_seconds === 0
                    }
                  />
                </Field>
              </FieldGroup>
            </CardContent>
          </Card>

          {/* DNS Server Settings */}
          <DNSServerSettings
            dnsServer={formData.dns_server}
            onChange={(dnsServer) =>
              setFormData((prev) => ({
                ...prev,
                dns_server: dnsServer,
              }))
            }
          />
        </>
      )}
    </BaseSettingsForm>
  );
}
