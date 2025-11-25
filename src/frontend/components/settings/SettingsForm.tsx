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
      ttl_override: 0,
      remap_53_interfaces: ['br0', 'br1'],
    },
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

  const addUpstream = () => {
    setFormData((prev) => ({
      ...prev,
      dns_server: {
        ...prev.dns_server,
        upstreams: [...prev.dns_server.upstreams, ''],
      },
    }));
  };

  const updateUpstream = (index: number, value: string) => {
    setFormData((prev) => ({
      ...prev,
      dns_server: {
        ...prev.dns_server,
        upstreams: prev.dns_server.upstreams.map((u, i) => i === index ? value : u),
      },
    }));
  };

  const removeUpstream = (index: number) => {
    setFormData((prev) => ({
      ...prev,
      dns_server: {
        ...prev.dns_server,
        upstreams: prev.dns_server.upstreams.filter((_, i) => i !== index),
      },
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
                onChange={(e) => setFormData((prev) => ({ ...prev, lists_output_dir: e.target.value }))}
                placeholder={t('settings.listsOutputDirPlaceholder')}
                required
              />
            </Field>

            {/* Interface Monitoring Interval */}
            <Field>
              <FieldLabel htmlFor="interface_monitoring_interval_seconds">
                {t('settings.interfaceMonitoringInterval', { defaultValue: 'Interface Monitoring Interval (seconds)' })}
              </FieldLabel>
              <FieldDescription>
                {t('settings.interfaceMonitoringIntervalDescription', { defaultValue: 'Interval in seconds to monitor interface changes (0 = disabled)' })}
              </FieldDescription>
              <Input
                id="interface_monitoring_interval_seconds"
                type="number"
                min="0"
                value={formData.interface_monitoring_interval_seconds}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10) || 0;
                  setFormData((prev) => ({ ...prev, interface_monitoring_interval_seconds: value }));
                }}
                placeholder="0"
              />
            </Field>

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
                <div className="flex flex-col">
                  <FieldLabel htmlFor="auto_update_enabled" className="cursor-pointer">
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

      {/* DNS Server Settings */}
      <Card>
        <CardHeader>
          <CardTitle>DNS Server Settings</CardTitle>
          <CardDescription>
            Configure the transparent DNS proxy for domain-based routing
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
                <div className="flex flex-col">
                  <FieldLabel htmlFor="dns_server_enable" className="cursor-pointer">
                    Enable DNS Server
                  </FieldLabel>
                  <FieldDescription>
                    Enable transparent DNS proxy for domain-based routing
                  </FieldDescription>
                </div>
              </div>
            </Field>

            {/* DNS Listen Address */}
            <Field>
              <FieldLabel htmlFor="dns_listen_addr">
                DNS Listen Address
              </FieldLabel>
              <FieldDescription>
                Listen address for DNS server (default: [::] for dual-stack)
              </FieldDescription>
              <Input
                id="dns_listen_addr"
                type="text"
                value={formData.dns_server.listen_addr}
                onChange={(e) => setFormData((prev) => ({
                  ...prev,
                  dns_server: { ...prev.dns_server, listen_addr: e.target.value },
                }))}
                placeholder="[::]"
                disabled={!formData.dns_server.enable}
              />
            </Field>

            {/* DNS Listen Port */}
            <Field>
              <FieldLabel htmlFor="dns_listen_port">
                DNS Listen Port
              </FieldLabel>
              <FieldDescription>
                Port for DNS server listener (default: 15353)
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
                placeholder="15353"
                disabled={!formData.dns_server.enable}
              />
            </Field>

            {/* DNS Remap 53 Interfaces */}
            <Field>
              <FieldLabel>Remap Port 53 Interfaces</FieldLabel>
              <FieldDescription>
                Interfaces to redirect DNS traffic (port 53) to the DNS server using iptables
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

            {/* DNS Upstreams */}
            <Field>
              <FieldLabel>DNS Upstream Servers</FieldLabel>
              <FieldDescription>
                Upstream DNS servers. Supported: keenetic://, udp://ip:port, doh://host/path
              </FieldDescription>
              <div className="space-y-2">
                {formData.dns_server.upstreams.map((upstream, index) => (
                  <div key={index} className="flex items-center gap-2">
                    <Input
                      value={upstream}
                      onChange={(e) => updateUpstream(index, e.target.value)}
                      placeholder="keenetic:// or udp://8.8.8.8:53 or doh://dns.google/dns-query"
                      disabled={!formData.dns_server.enable}
                    />
                    <Button
                      type="button"
                      variant="ghost"
                      size="sm"
                      onClick={() => removeUpstream(index)}
                      disabled={!formData.dns_server.enable || formData.dns_server.upstreams.length <= 1}
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
                  disabled={!formData.dns_server.enable}
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
                value={formData.dns_server.cache_max_domains}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10) || 1000;
                  setFormData((prev) => ({
                    ...prev,
                    dns_server: { ...prev.dns_server, cache_max_domains: value },
                  }));
                }}
                placeholder="1000"
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
                value={formData.dns_server.ttl_override}
                onChange={(e) => {
                  const value = parseInt(e.target.value, 10) || 0;
                  setFormData((prev) => ({
                    ...prev,
                    dns_server: { ...prev.dns_server, ttl_override: value },
                  }));
                }}
                placeholder="0"
                disabled={!formData.dns_server.enable}
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
