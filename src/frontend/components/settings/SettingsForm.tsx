import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2, Plus, X } from 'lucide-react';
import { useSettings, useUpdateSettings } from '../../src/hooks/useSettings';
import { Card, CardHeader, CardTitle, CardDescription, CardContent, CardFooter } from '../ui/card';
import { Field, FieldLabel, FieldDescription, FieldGroup } from '../ui/field';
import { Input } from '../ui/input';
import { Checkbox } from '../ui/checkbox';
import { Button } from '../ui/button';
import { Alert } from '../ui/alert';
import { InterfaceSelector } from '../ui/interface-selector';
import type { GeneralSettings } from '../../src/api/client';

export function SettingsForm() {
  const { t } = useTranslation();
  const { data: settings, isLoading, error } = useSettings();
  const updateSettings = useUpdateSettings();

  // Form state
  const [formData, setFormData] = useState<GeneralSettings>({
    lists_output_dir: '',
    use_keenetic_dns: true,
    fallback_dns: '',
    auto_update_lists: true,
    update_interval_hours: 24,
    enable_interface_monitoring: false,
    enable_dns_proxy: true,
    dns_proxy_listen_addr: '[::]',
    dns_proxy_port: 15353,
    dns_upstream: ['keenetic://'],
    dns_cache_max_domains: 1000,
    drop_aaaa: false,
    ttl_override: 0,
    dns_proxy_interfaces: ['br0', 'br1'],
    dns_proxy_remap_53: true,
  });

  // Sync form data with fetched settings
  useEffect(() => {
    if (settings) {
      setFormData(settings);
    }
  }, [settings]);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    try {
      await updateSettings.mutateAsync(formData);
      toast.success(t('common.success'), {
        description: t('settings.saveSuccess'),
      });
    } catch (error) {
      toast.error(t('common.error'), {
        description: error instanceof Error ? error.message : t('settings.saveError'),
      });
    }
  };

  const handleInputChange = (field: keyof GeneralSettings) => (
    e: React.ChangeEvent<HTMLInputElement>
  ) => {
    setFormData((prev) => ({
      ...prev,
      [field]: e.target.value,
    }));
  };

  const handleCheckboxChange = (field: keyof GeneralSettings) => (
    checked: boolean
  ) => {
    setFormData((prev) => ({
      ...prev,
      [field]: checked,
    }));
  };

  const addUpstream = () => {
    setFormData((prev) => ({
      ...prev,
      dns_upstream: [...(prev.dns_upstream || []), ''],
    }));
  };

  const updateUpstream = (index: number, value: string) => {
    setFormData((prev) => ({
      ...prev,
      dns_upstream: (prev.dns_upstream || []).map((u, i) => i === index ? value : u),
    }));
  };

  const removeUpstream = (index: number) => {
    setFormData((prev) => ({
      ...prev,
      dns_upstream: (prev.dns_upstream || []).filter((_, i) => i !== index),
    }));
  };

  if (isLoading) {
    return (
      <div className="flex items-center justify-center py-12">
        <Loader2 className="h-8 w-8 animate-spin text-muted-foreground" />
      </div>
    );
  }

  if (error) {
    return (
      <Alert variant="destructive">
        <div>
          <strong>{t('common.error')}</strong>
          <p className="mt-1 text-sm">
            {error instanceof Error ? error.message : t('settings.loadError')}
          </p>
        </div>
      </Alert>
    );
  }

  return (
    <form onSubmit={handleSubmit} className="space-y-6">
      {/* General Settings */}
      <Card>
        <CardHeader>
          <CardTitle>{t('settings.title')}</CardTitle>
          <CardDescription>
            {t('settings.description')}
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
                onChange={handleInputChange('lists_output_dir')}
                placeholder={t('settings.listsOutputDirPlaceholder')}
                required
              />
            </Field>

            {/* Use Keenetic DNS */}
            <Field orientation="horizontal">
              <div className="flex items-center space-x-3">
                <Checkbox
                  id="use_keenetic_dns"
                  checked={formData.use_keenetic_dns}
                  onCheckedChange={handleCheckboxChange('use_keenetic_dns')}
                />
                <div className="flex flex-col">
                  <FieldLabel htmlFor="use_keenetic_dns" className="cursor-pointer">
                    {t('settings.useKeeneticDns')}
                  </FieldLabel>
                  <FieldDescription>
                    {t('settings.useKeeneticDnsDescription')}
                  </FieldDescription>
                </div>
              </div>
            </Field>

            {/* Fallback DNS */}
            <Field>
              <FieldLabel htmlFor="fallback_dns">
                {t('settings.fallbackDns')}
              </FieldLabel>
              <FieldDescription>
                {t('settings.fallbackDnsDescription')}
              </FieldDescription>
              <Input
                id="fallback_dns"
                type="text"
                value={formData.fallback_dns || ''}
                onChange={handleInputChange('fallback_dns')}
                placeholder={t('settings.fallbackDnsPlaceholder')}
              />
            </Field>

            {/* Auto-update Lists */}
            <Field orientation="horizontal">
              <div className="flex items-center space-x-3">
                <Checkbox
                  id="auto_update_lists"
                  checked={formData.auto_update_lists ?? true}
                  onCheckedChange={handleCheckboxChange('auto_update_lists')}
                />
                <div className="flex flex-col">
                  <FieldLabel htmlFor="auto_update_lists" className="cursor-pointer">
                    {t('settings.autoUpdateLists')}
                  </FieldLabel>
                  <FieldDescription>
                    {t('settings.autoUpdateListsDescription')}
                  </FieldDescription>
                </div>
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
                value={formData.update_interval_hours ?? 24}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10);
                  setFormData((prev) => ({
                    ...prev,
                    update_interval_hours: value,
                  }));
                }}
                placeholder="24"
                disabled={!formData.auto_update_lists}
              />
            </Field>

            {/* Enable Interface Monitoring */}
            <Field orientation="horizontal">
              <div className="flex items-center space-x-3">
                <Checkbox
                  id="enable_interface_monitoring"
                  checked={formData.enable_interface_monitoring ?? false}
                  onCheckedChange={handleCheckboxChange('enable_interface_monitoring')}
                />
                <div className="flex flex-col">
                  <FieldLabel htmlFor="enable_interface_monitoring" className="cursor-pointer">
                    {t('settings.enableInterfaceMonitoring', { defaultValue: 'Enable Interface Monitoring' })}
                  </FieldLabel>
                  <FieldDescription>
                    {t('settings.enableInterfaceMonitoringDescription', { defaultValue: 'Enable keen-pbr service to monitor interface state changes and automatically reapply routing (checks every 10 seconds)' })}
                  </FieldDescription>
                </div>
              </div>
            </Field>
          </FieldGroup>
        </CardContent>

        <CardFooter className="justify-end">
          <Button
            type="submit"
            disabled={updateSettings.isPending}
          >
            {updateSettings.isPending && (
              <Loader2 className="mr-2 h-4 w-4 animate-spin" />
            )}
            {t('settings.saveChanges')}
          </Button>
        </CardFooter>
      </Card>

      {/* DNS Proxy Settings */}
      <Card>
        <CardHeader>
          <CardTitle>DNS Proxy Settings</CardTitle>
          <CardDescription>
            Configure the transparent DNS proxy for domain-based routing
          </CardDescription>
        </CardHeader>

        <CardContent>
          <FieldGroup>
            {/* Enable DNS Proxy */}
            <Field orientation="horizontal">
              <div className="flex items-center space-x-3">
                <Checkbox
                  id="enable_dns_proxy"
                  checked={formData.enable_dns_proxy ?? true}
                  onCheckedChange={handleCheckboxChange('enable_dns_proxy')}
                />
                <div className="flex flex-col">
                  <FieldLabel htmlFor="enable_dns_proxy" className="cursor-pointer">
                    Enable DNS Proxy
                  </FieldLabel>
                  <FieldDescription>
                    Enable transparent DNS proxy for domain-based routing
                  </FieldDescription>
                </div>
              </div>
            </Field>

            {/* DNS Proxy Port */}
            <Field>
              <FieldLabel htmlFor="dns_proxy_port">
                DNS Proxy Port
              </FieldLabel>
              <FieldDescription>
                Port for DNS proxy listener (default: 15353)
              </FieldDescription>
              <Input
                id="dns_proxy_port"
                type="number"
                min="1"
                max="65535"
                value={formData.dns_proxy_port ?? 15353}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10);
                  setFormData((prev) => ({
                    ...prev,
                    dns_proxy_port: value,
                  }));
                }}
                placeholder="15353"
                disabled={!formData.enable_dns_proxy}
              />
            </Field>

            {/* Remap Port 53 */}
            <Field orientation="horizontal">
              <div className="flex items-center space-x-3">
                <Checkbox
                  id="dns_proxy_remap_53"
                  checked={formData.dns_proxy_remap_53 ?? true}
                  onCheckedChange={handleCheckboxChange('dns_proxy_remap_53')}
                  disabled={!formData.enable_dns_proxy}
                />
                <div className="flex flex-col">
                  <FieldLabel htmlFor="dns_proxy_remap_53" className="cursor-pointer">
                    Remap Port 53
                  </FieldLabel>
                  <FieldDescription>
                    Redirect DNS traffic (port 53) to the DNS proxy port using iptables
                  </FieldDescription>
                </div>
              </div>
            </Field>

            {/* Drop AAAA */}
            <Field orientation="horizontal">
              <div className="flex items-center space-x-3">
                <Checkbox
                  id="drop_aaaa"
                  checked={formData.drop_aaaa ?? false}
                  onCheckedChange={handleCheckboxChange('drop_aaaa')}
                  disabled={!formData.enable_dns_proxy}
                />
                <div className="flex flex-col">
                  <FieldLabel htmlFor="drop_aaaa" className="cursor-pointer">
                    Drop AAAA Records
                  </FieldLabel>
                  <FieldDescription>
                    Drop IPv6 (AAAA) DNS responses to force IPv4
                  </FieldDescription>
                </div>
              </div>
            </Field>

            {/* DNS Proxy Interfaces */}
            <Field>
              <FieldLabel>DNS Proxy Interfaces</FieldLabel>
              <FieldDescription>
                Interfaces to intercept DNS traffic on (default: br0, br1)
              </FieldDescription>
              <InterfaceSelector
                value={formData.dns_proxy_interfaces || []}
                onChange={(interfaces) => setFormData((prev) => ({ ...prev, dns_proxy_interfaces: interfaces }))}
                allowReorder={false}
              />
            </Field>

            {/* DNS Upstreams */}
            <Field>
              <FieldLabel>DNS Upstream Servers</FieldLabel>
              <FieldDescription>
                Upstream DNS servers. Supported: keenetic://, udp://ip:port, doh://host/path
              </FieldDescription>
              <div className="space-y-2">
                {(formData.dns_upstream || []).map((upstream, index) => (
                  <div key={index} className="flex items-center gap-2">
                    <Input
                      value={upstream}
                      onChange={(e) => updateUpstream(index, e.target.value)}
                      placeholder="keenetic:// or udp://8.8.8.8:53 or doh://dns.google/dns-query"
                      disabled={!formData.enable_dns_proxy}
                    />
                    <Button
                      type="button"
                      variant="ghost"
                      size="sm"
                      onClick={() => removeUpstream(index)}
                      disabled={!formData.enable_dns_proxy || (formData.dns_upstream?.length || 0) <= 1}
                    >
                      <X className="h-4 w-4" />
                    </Button>
                  </div>
                ))}
                <Button
                  type="button"
                  variant="outline"
                  size="sm"
                  onClick={addUpstream}
                  disabled={!formData.enable_dns_proxy}
                >
                  <Plus className="mr-2 h-4 w-4" />
                  Add Upstream
                </Button>
              </div>
            </Field>

            {/* DNS Cache Max Domains */}
            <Field>
              <FieldLabel htmlFor="dns_cache_max_domains">
                DNS Cache Max Domains
              </FieldLabel>
              <FieldDescription>
                Maximum number of domains to cache (default: 1000)
              </FieldDescription>
              <Input
                id="dns_cache_max_domains"
                type="number"
                min="100"
                value={formData.dns_cache_max_domains ?? 1000}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10);
                  setFormData((prev) => ({
                    ...prev,
                    dns_cache_max_domains: value,
                  }));
                }}
                placeholder="1000"
                disabled={!formData.enable_dns_proxy}
              />
            </Field>

            {/* TTL Override */}
            <Field>
              <FieldLabel htmlFor="ttl_override">
                TTL Override (seconds)
              </FieldLabel>
              <FieldDescription>
                Override TTL for DNS responses (0 = use original TTL)
              </FieldDescription>
              <Input
                id="ttl_override"
                type="number"
                min="0"
                value={formData.ttl_override ?? 0}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10);
                  setFormData((prev) => ({
                    ...prev,
                    ttl_override: value,
                  }));
                }}
                placeholder="0"
                disabled={!formData.enable_dns_proxy}
              />
            </Field>
          </FieldGroup>
        </CardContent>

        <CardFooter className="justify-end">
          <Button
            type="submit"
            disabled={updateSettings.isPending}
          >
            {updateSettings.isPending && (
              <Loader2 className="mr-2 h-4 w-4 animate-spin" />
            )}
            {t('settings.saveChanges')}
          </Button>
        </CardFooter>
      </Card>
    </form>
  );
}
