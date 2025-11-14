import { useEffect, useState } from 'react';
import { settingsAPI } from '../api/client';
import type { DNS, GeneralSettings as GeneralSettingsType } from '../types';
import { DNSServerInput } from '../components/DNSServerInput';
import { Button } from '../components/ui/Button';

export function GeneralSettings() {
  const [settings, setSettings] = useState<GeneralSettingsType>({});
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [successMessage, setSuccessMessage] = useState<string | null>(null);

  useEffect(() => {
    loadSettings();
  }, []);

  const loadSettings = async () => {
    try {
      setLoading(true);
      setError(null);
      const data = await settingsAPI.getGeneral();
      setSettings(data || {});
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load settings');
    } finally {
      setLoading(false);
    }
  };

  const handleSave = async () => {
    try {
      setSaving(true);
      setError(null);
      setSuccessMessage(null);
      await settingsAPI.updateGeneral(settings);
      setSuccessMessage('Settings saved successfully');
      setTimeout(() => setSuccessMessage(null), 3000);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to save settings');
    } finally {
      setSaving(false);
    }
  };

  const handleDefaultDNSChange = (dns: DNS | undefined) => {
    setSettings((prev) => ({
      ...prev,
      defaultDnsServer: dns,
    }));
  };

  const handleBootstrapDNSChange = (dns: DNS | undefined) => {
    setSettings((prev) => ({
      ...prev,
      bootstrapDnsServer: dns,
    }));
  };

  if (loading) {
    return (
      <div className="max-w-4xl mx-auto p-6">
        <div className="text-center py-8">Loading settings...</div>
      </div>
    );
  }

  return (
    <div className="max-w-4xl mx-auto p-6">
      <div className="mb-6">
        <h1 className="text-3xl font-bold text-gray-900">General Settings</h1>
        <p className="text-gray-600 mt-2">
          Configure default DNS servers used by the routing system
        </p>
      </div>

      {error && (
        <div className="mb-4 p-4 bg-red-50 border border-red-200 rounded-md">
          <p className="text-red-800">{error}</p>
        </div>
      )}

      {successMessage && (
        <div className="mb-4 p-4 bg-green-50 border border-green-200 rounded-md">
          <p className="text-green-800">{successMessage}</p>
        </div>
      )}

      <div className="bg-white rounded-lg shadow p-6 space-y-8">
        {/* Default DNS Server */}
        <div>
          <DNSServerInput
            value={settings.defaultDnsServer}
            onChange={handleDefaultDNSChange}
            label="Default DNS Server"
            allowEmpty={true}
          />
          <p className="mt-2 text-sm text-gray-500">
            This DNS server will be used as the default for DNS resolution. If not set, rules may use their custom DNS servers.
          </p>
        </div>

        <hr className="border-gray-200" />

        {/* Bootstrap DNS Server */}
        <div>
          <DNSServerInput
            value={settings.bootstrapDnsServer}
            onChange={handleBootstrapDNSChange}
            label="Bootstrap DNS Server"
            allowEmpty={true}
          />
          <p className="mt-2 text-sm text-gray-500">
            This DNS server is used to resolve domain names of other DNS servers. Typically should be a plain UDP DNS server (like 8.8.8.8:53).
          </p>
        </div>

        {/* Save Button */}
        <div className="flex justify-end pt-4 border-t border-gray-200">
          <Button onClick={handleSave} disabled={saving} size="lg">
            {saving ? 'Saving...' : 'Save Settings'}
          </Button>
        </div>
      </div>
    </div>
  );
}
