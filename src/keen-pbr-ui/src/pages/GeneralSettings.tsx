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
  const [newInterface, setNewInterface] = useState('');

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

  const handleAddInterface = () => {
    if (newInterface.trim() === '') return;

    setSettings((prev) => ({
      ...prev,
      inboundInterfaces: [...(prev.inboundInterfaces || []), newInterface.trim()],
    }));
    setNewInterface('');
  };

  const handleRemoveInterface = (index: number) => {
    setSettings((prev) => ({
      ...prev,
      inboundInterfaces: (prev.inboundInterfaces || []).filter((_, i) => i !== index),
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

        <hr className="border-gray-200" />

        {/* Inbound Interfaces */}
        <div>
          <label className="block text-sm font-medium text-gray-700 mb-2">
            Inbound Interfaces
          </label>
          <p className="text-sm text-gray-500 mb-3">
            Network interfaces to apply routing rules to (e.g., br-lan, gre4-mygre). Leave empty to default to br-lan.
          </p>

          {/* List of interfaces */}
          <div className="space-y-2 mb-3">
            {(settings.inboundInterfaces || []).length === 0 ? (
              <p className="text-sm text-gray-400 italic">No interfaces configured (will default to br-lan)</p>
            ) : (
              (settings.inboundInterfaces || []).map((iface, index) => (
                <div key={index} className="flex items-center gap-2">
                  <input
                    type="text"
                    value={iface}
                    disabled
                    className="flex-1 px-3 py-2 border border-gray-300 rounded-md bg-gray-50 text-gray-700"
                  />
                  <Button
                    onClick={() => handleRemoveInterface(index)}
                    variant="secondary"
                    size="sm"
                  >
                    Remove
                  </Button>
                </div>
              ))
            )}
          </div>

          {/* Add interface input */}
          <div className="flex items-center gap-2">
            <input
              type="text"
              value={newInterface}
              onChange={(e) => setNewInterface(e.target.value)}
              onKeyPress={(e) => {
                if (e.key === 'Enter') {
                  e.preventDefault();
                  handleAddInterface();
                }
              }}
              placeholder="Enter interface name (e.g., br-lan)"
              className="flex-1 px-3 py-2 border border-gray-300 rounded-md focus:ring-2 focus:ring-blue-500 focus:border-transparent"
            />
            <Button
              onClick={handleAddInterface}
              variant="primary"
              size="sm"
              disabled={newInterface.trim() === ''}
            >
              Add
            </Button>
          </div>
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
