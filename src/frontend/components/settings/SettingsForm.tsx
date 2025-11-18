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
    ListsOutputDir: '',
    UseKeeneticDNS: true,
    FallbackDNS: '',
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
        description: 'Settings saved successfully',
      });
    } catch (error) {
      toast.error(t('common.error'), {
        description: error instanceof Error ? error.message : 'Failed to save settings',
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
            {error instanceof Error ? error.message : 'Failed to load settings'}
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
            Manage global configuration settings for keen-pbr
          </CardDescription>
        </CardHeader>

        <CardContent>
          <FieldGroup>
            {/* Lists Output Directory */}
            <Field>
              <FieldLabel htmlFor="ListsOutputDir">
                {t('settings.listsOutputDir')}
              </FieldLabel>
              <FieldDescription>
                Directory where downloaded list files will be stored
              </FieldDescription>
              <Input
                id="ListsOutputDir"
                type="text"
                value={formData.ListsOutputDir}
                onChange={handleInputChange('ListsOutputDir')}
                placeholder="/opt/etc/keen-pbr/lists.d"
                required
              />
            </Field>

            {/* Use Keenetic DNS */}
            <Field orientation="horizontal">
              <div className="flex items-center space-x-3">
                <Checkbox
                  id="UseKeeneticDNS"
                  checked={formData.UseKeeneticDNS}
                  onCheckedChange={handleCheckboxChange('UseKeeneticDNS')}
                />
                <div className="flex flex-col">
                  <FieldLabel htmlFor="UseKeeneticDNS" className="cursor-pointer">
                    {t('settings.useKeeneticDns')}
                  </FieldLabel>
                  <FieldDescription>
                    Use Keenetic DNS from System profile as upstream in generated dnsmasq config
                  </FieldDescription>
                </div>
              </div>
            </Field>

            {/* Fallback DNS */}
            <Field>
              <FieldLabel htmlFor="FallbackDNS">
                {t('settings.fallbackDns')}
              </FieldLabel>
              <FieldDescription>
                Fallback DNS server to use if Keenetic RCI call fails (e.g., 8.8.8.8 or 1.1.1.1). Leave empty to disable.
              </FieldDescription>
              <Input
                id="FallbackDNS"
                type="text"
                value={formData.FallbackDNS || ''}
                onChange={handleInputChange('FallbackDNS')}
                placeholder="8.8.8.8"
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
