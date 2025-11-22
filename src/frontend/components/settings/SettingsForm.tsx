import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2 } from 'lucide-react';
import { useSettings, useUpdateSettings } from '../../src/hooks/useSettings';
import { Card, CardHeader, CardTitle, CardDescription, CardContent, CardFooter } from '../ui/card';
import { Field, FieldLabel, FieldDescription, FieldGroup } from '../ui/field';
import { Input } from '../ui/input';
import { Checkbox } from '../ui/checkbox';
import { Button } from '../ui/button';
import { Alert } from '../ui/alert';
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
    <form onSubmit={handleSubmit}>
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
    </form>
  );
}
