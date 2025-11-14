import { useEffect, useState } from 'react';
import { configAPI } from '../api/client';
import type { Config } from '../types';

export default function Configuration() {
  const [config, setConfig] = useState<Config | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const loadConfig = async () => {
    try {
      setError(null);
      const data = await configAPI.get();
      setConfig(data);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load configuration');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadConfig();
  }, []);

  if (loading) {
    return <div className="px-4 py-6">Loading...</div>;
  }

  return (
    <div className="px-4 py-6 sm:px-0">
      <div className="bg-white shadow rounded-lg">
        <div className="px-6 py-4 border-b border-gray-200">
          <h2 className="text-2xl font-bold text-gray-900">Configuration</h2>
        </div>

        {error && (
          <div className="m-6 p-4 bg-red-50 border border-red-200 rounded-md">
            <p className="text-red-800">{error}</p>
          </div>
        )}

        {config && (
          <div className="p-6">
            <pre className="bg-gray-50 p-4 rounded-md overflow-x-auto text-sm">
              {JSON.stringify(config, null, 2)}
            </pre>
          </div>
        )}
      </div>
    </div>
  );
}
