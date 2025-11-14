import { useEffect, useState } from 'react';
import { configAPI } from '../api/client';
import type { Config } from '../types';
import { ToggleButtonGroup } from '../components/ui/ToggleButtonGroup';

type ConfigType = 'keen-pbr' | 'singbox';

export default function Configuration() {
  const [configType, setConfigType] = useState<ConfigType>('keen-pbr');
  const [config, setConfig] = useState<Config | null>(null);
  const [singboxConfig, setSingboxConfig] = useState<any>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const loadConfig = async (type: ConfigType) => {
    try {
      setLoading(true);
      setError(null);
      if (type === 'keen-pbr') {
        const data = await configAPI.get();
        setConfig(data);
      } else {
        const data = await configAPI.getSingbox();
        setSingboxConfig(data);
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load configuration');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadConfig(configType);
  }, [configType]);

  return (
    <div className="px-4 py-6 sm:px-0">
      <div className="bg-white shadow rounded-lg">
        <div className="px-6 py-4 border-b border-gray-200">
          <div className="flex items-center justify-between mb-4">
            <h2 className="text-2xl font-bold text-gray-900">Configuration</h2>
          </div>

          <ToggleButtonGroup
            options={[
              { value: 'keen-pbr' as ConfigType, label: 'keen-pbr Config' },
              { value: 'singbox' as ConfigType, label: 'sing-box Config' },
            ]}
            value={configType}
            onChange={setConfigType}
          />
        </div>

        {error && (
          <div className="m-6 p-4 bg-red-50 border border-red-200 rounded-md">
            <p className="text-red-800">{error}</p>
          </div>
        )}

        {loading ? (
          <div className="p-6 text-center text-gray-500">Loading...</div>
        ) : (
          <div className="p-6">
            <pre className="bg-gray-50 p-4 rounded-md overflow-x-auto text-sm">
              {JSON.stringify(configType === 'keen-pbr' ? config : singboxConfig, null, 2)}
            </pre>
          </div>
        )}
      </div>
    </div>
  );
}
