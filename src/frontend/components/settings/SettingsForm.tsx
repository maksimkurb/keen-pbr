import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { useSettings, useUpdateSettings } from '../../src/hooks/useSettings';
import { Card, CardHeader, CardTitle, CardDescription, CardContent } from '../ui/card';
import { Field, FieldLabel, FieldDescription, FieldGroup, FieldSeparator } from '../ui/field';
import { Input } from '../ui/input';
import { Checkbox } from '../ui/checkbox';
import { InterfaceSelector } from '../ui/interface-selector';
import { StringArrayInput } from '../ui/string-array-input';
import { BaseSettingsForm } from '../ui/base-settings-form';
import type { GeneralSettings } from '../../src/api/client';

export function SettingsForm() {
  const { t } = useTranslation();
  const { data: settings, isLoading, error } = useSettings();
  const updateSettings = useUpdateSettings();

  const defaultData: GeneralSettings = {
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

  const handleSave = async (formData: GeneralSettings) => {
    try {
      await updateSettings.mutateAsync(formData);
      toast.success(t('common.success'), {
        description: t('settings.saveSuccess'),
      });
    } catch (error) {
      toast.error(t('common.error'), {
        description: error instanceof Error ? error.message : t('settings.saveError'),
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
                onChange={(e) => setFormData((prev) => ({ ...prev, lists_output_dir: e.target.value }))}
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
                  checked={formData.auto_update_lists.enabled}
                  onCheckedChange={(checked) => setFormData((prev) => ({
                    ...prev,
                    auto_update_lists: { ...prev.auto_update_lists, enabled: !!checked },
                  }))}
                />
                <FieldLabel htmlFor="auto_update_enabled" className="cursor-pointer flex flex-col items-start gap-0">
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
                value={formData.auto_update_lists.interval_hours}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10) || 1;
                  setFormData((prev) => ({
                    ...prev,
                    auto_update_lists: { ...prev.auto_update_lists, interval_hours: value },
                  }));
                }}
                placeholder="24"
                disabled={!formData.auto_update_lists.enabled}
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
                  checked={formData.interface_monitoring_interval_seconds > 0}
                  onCheckedChange={(checked) => setFormData((prev) => ({
                    ...prev,
                    interface_monitoring_interval_seconds: checked ? 10 : 0,
                  }))}
                />
                <FieldLabel htmlFor="enable_interface_monitoring" className="cursor-pointer flex flex-col items-start gap-0">
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
                  setFormData((prev) => ({ ...prev, interface_monitoring_interval_seconds: Math.max(10, value) }));
                }}
                placeholder="10"
                disabled={formData.interface_monitoring_interval_seconds === 0}
              />
            </Field>
          </FieldGroup>
        </CardContent>
      </Card>

      {/* DNS Server Settings */}
      <Card>
        <CardHeader>
          <CardTitle>{t('settings.dnsServerTitle')}</CardTitle>
          <CardDescription>
            {t('settings.dnsServerDescription')}
          </CardDescription>
        </CardHeader>

        <CardContent>
          <FieldGroup>
            {/* Enable DNS Server */}
            <Field orientation="horizontal">
              <div className="flex items-center space-x-3">
                <Checkbox
                  id="dns_server_enable"
                  checked={formData.dns_server.enable}
                  onCheckedChange={(checked) => setFormData((prev) => ({
                    ...prev,
                    dns_server: { ...prev.dns_server, enable: !!checked },
                  }))}
                />
                <FieldLabel htmlFor="dns_server_enable" className="cursor-pointer flex flex-col items-start gap-0">
                  {t('settings.enableDnsServer')}
                  <FieldDescription>
                    {t('settings.enableDnsServerDescription')}
                  </FieldDescription>
                </FieldLabel>
              </div>
            </Field>

            {/* DNS Listen Address */}
            <Field>
              <FieldLabel htmlFor="dns_listen_addr">
                {t('settings.dnsListenAddr')}
              </FieldLabel>
              <FieldDescription>
                {t('settings.dnsListenAddrDescription')}
              </FieldDescription>
              <Input
                id="dns_listen_addr"
                type="text"
                value={formData.dns_server.listen_addr}
                onChange={(e) => setFormData((prev) => ({
                  ...prev,
                  dns_server: { ...prev.dns_server, listen_addr: e.target.value },
                }))}
                placeholder={t('settings.dnsListenAddrPlaceholder')}
                disabled={!formData.dns_server.enable}
              />
            </Field>

            {/* DNS Listen Port */}
            <Field>
              <FieldLabel htmlFor="dns_listen_port">
                {t('settings.dnsListenPort')}
              </FieldLabel>
              <FieldDescription>
                {t('settings.dnsListenPortDescription')}
              </FieldDescription>
              <Input
                id="dns_listen_port"
                type="number"
                min="1"
                max="65535"
                value={formData.dns_server.listen_port}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10) || 15353;
                  setFormData((prev) => ({
                    ...prev,
                    dns_server: { ...prev.dns_server, listen_port: value },
                  }));
                }}
                placeholder={t('settings.dnsListenPortPlaceholder')}
                disabled={!formData.dns_server.enable}
              />
            </Field>

            <FieldSeparator />

            {/* DNS Upstreams */}
            <Field>
              <FieldLabel>{t('settings.dnsUpstreamServers')}</FieldLabel>
              <FieldDescription>
                {t('settings.dnsUpstreamServersDescription')}
              </FieldDescription>
              <StringArrayInput
                value={formData.dns_server.upstreams}
                onChange={(upstreams) => setFormData((prev) => ({
                  ...prev,
                  dns_server: { ...prev.dns_server, upstreams },
                }))}
                placeholder={t('settings.dnsUpstreamPlaceholder')}
                disabled={!formData.dns_server.enable}
                minItems={1}
                addButtonLabel={t('settings.addUpstream')}
              />
            </Field>

            <FieldSeparator />

            {/* DNS Cache Max Domains */}
            <Field>
              <FieldLabel htmlFor="dns_cache_max_domains">
                {t('settings.dnsCacheMaxDomains')}
              </FieldLabel>
              <FieldDescription>
                {t('settings.dnsCacheMaxDomainsDescription')}
              </FieldDescription>
              <Input
                id="dns_cache_max_domains"
                type="number"
                min="100"
                value={formData.dns_server.cache_max_domains}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10) || 1000;
                  setFormData((prev) => ({
                    ...prev,
                    dns_server: { ...prev.dns_server, cache_max_domains: value },
                  }));
                }}
                placeholder={t('settings.dnsCacheMaxDomainsPlaceholder')}
                disabled={!formData.dns_server.enable}
              />
            </Field>

            {/* Drop AAAA */}
            <Field orientation="horizontal">
              <div className="flex items-center space-x-3">
                <Checkbox
                  id="drop_aaaa"
                  checked={formData.dns_server.drop_aaaa}
                  onCheckedChange={(checked) => setFormData((prev) => ({
                    ...prev,
                    dns_server: { ...prev.dns_server, drop_aaaa: !!checked },
                  }))}
                  disabled={!formData.dns_server.enable}
                />
                <FieldLabel htmlFor="drop_aaaa" className="cursor-pointer flex flex-col items-start gap-0">
                  {t('settings.dropAAAA')}
                  <FieldDescription>
                    {t('settings.dropAAAADescription')}
                  </FieldDescription>
                </FieldLabel>
              </div>
            </Field>

            {/* IPSet Entry Additional TTL */}
            <Field>
              <FieldLabel htmlFor="ipset_entry_additional_ttl_sec">
                {t('settings.ipsetEntryAdditionalTTL')}
              </FieldLabel>
              <FieldDescription>
                {t('settings.ipsetEntryAdditionalTTLDescription')}
              </FieldDescription>
              <Input
                id="ipset_entry_additional_ttl_sec"
                type="number"
                min="0"
                max="2147483"
                value={formData.dns_server.ipset_entry_additional_ttl_sec}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10) || 0;
                  setFormData((prev) => ({
                    ...prev,
                    dns_server: { ...prev.dns_server, ipset_entry_additional_ttl_sec: value },
                  }));
                }}
                placeholder="7200"
                disabled={!formData.dns_server.enable}
              />
            </Field>

            {/* Listed Domains DNS Cache TTL */}
            <Field>
              <FieldLabel htmlFor="listed_domains_dns_cache_ttl_sec">
                {t('settings.listedDomainsDNSCacheTTL')}
              </FieldLabel>
              <FieldDescription>
                {t('settings.listedDomainsDNSCacheTTLDescription')}
              </FieldDescription>
              <Input
                id="listed_domains_dns_cache_ttl_sec"
                type="number"
                min="0"
                max="2147483"
                value={formData.dns_server.listed_domains_dns_cache_ttl_sec}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10) || 0;
                  setFormData((prev) => ({
                    ...prev,
                    dns_server: { ...prev.dns_server, listed_domains_dns_cache_ttl_sec: value },
                  }));
                }}
                placeholder="30"
                disabled={!formData.dns_server.enable}
              />
            </Field>

            <FieldSeparator />

            {/* DNS Remap 53 Interfaces */}
            <Field>
              <FieldLabel>{t('settings.dnsRemap53Interfaces')}</FieldLabel>
              <FieldDescription>
                {t('settings.dnsRemap53InterfacesDescription')}
              </FieldDescription>
              <InterfaceSelector
                value={formData.dns_server.remap_53_interfaces}
                onChange={(interfaces) => setFormData((prev) => ({
                  ...prev,
                  dns_server: { ...prev.dns_server, remap_53_interfaces: interfaces },
                }))}
                allowReorder={false}
              />
            </Field>
          </FieldGroup>
        </CardContent>
      </Card>
          </>
      )}
    </BaseSettingsForm>
  );
}
