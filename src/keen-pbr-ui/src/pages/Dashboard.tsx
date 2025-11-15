import { useEffect, useState } from 'react';
import { serviceAPI } from '../api/client';
import type { ServiceStatusResponse } from '../types';
import SingboxWidget from '../components/SingboxWidget';

export default function Dashboard() {
  const [status, setStatus] = useState<ServiceStatusResponse | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const loadStatus = async () => {
    try {
      setError(null);
      const data = await serviceAPI.getStatus();
      setStatus(data);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load status');
    }
  };

  useEffect(() => {
    loadStatus();
    const interval = setInterval(loadStatus, 5000);
    return () => clearInterval(interval);
  }, []);

  const handleAction = async (action: () => Promise<any>) => {
    setLoading(true);
    setError(null);
    try {
      await action();
      await loadStatus();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Action failed');
    } finally {
      setLoading(false);
    }
  };

  const getStatusColor = (status: string) => {
    switch (status) {
      case 'running':
        return 'bg-green-100 text-green-800';
      case 'stopped':
        return 'bg-red-100 text-red-800';
      case 'starting':
      case 'stopping':
        return 'bg-yellow-100 text-yellow-800';
      default:
        return 'bg-gray-100 text-gray-800';
    }
  };

  return (
    <div className="px-4 py-6 sm:px-0">
      <div className="space-y-6">
        {/* Service Status Widget */}
        <div className="bg-white shadow rounded-lg p-6">
          <h2 className="text-2xl font-bold text-gray-900 mb-6">Service Status</h2>

          {error && (
            <div className="mb-4 p-4 bg-red-50 border border-red-200 rounded-md">
              <p className="text-red-800">{error}</p>
            </div>
          )}

          {status && (
            <div className="mb-6">
              <div className="flex items-center space-x-4 mb-4">
                <span className="text-gray-700 font-medium">Status:</span>
                <span className={`px-3 py-1 rounded-full text-sm font-medium ${getStatusColor(status.status)}`}>
                  {status.status.toUpperCase()}
                </span>
              </div>
              <div className="flex items-center space-x-4">
                <span className="text-gray-700 font-medium">Autostart:</span>
                <span className={`px-3 py-1 rounded-full text-sm font-medium ${status.enabled ? 'bg-blue-100 text-blue-800' : 'bg-gray-100 text-gray-800'}`}>
                  {status.enabled ? 'ENABLED' : 'DISABLED'}
                </span>
              </div>
            </div>
          )}

          <div className="flex flex-wrap gap-3">
            <button
              onClick={() => handleAction(serviceAPI.start)}
              disabled={loading || status?.status === 'running'}
              className="px-4 py-2 bg-green-600 text-white rounded-md hover:bg-green-700 disabled:opacity-50 disabled:cursor-not-allowed"
            >
              Start
            </button>
            <button
              onClick={() => handleAction(serviceAPI.stop)}
              disabled={loading || status?.status === 'stopped'}
              className="px-4 py-2 bg-red-600 text-white rounded-md hover:bg-red-700 disabled:opacity-50 disabled:cursor-not-allowed"
            >
              Stop
            </button>
            <button
              onClick={() => handleAction(serviceAPI.restart)}
              disabled={loading}
              className="px-4 py-2 bg-yellow-600 text-white rounded-md hover:bg-yellow-700 disabled:opacity-50 disabled:cursor-not-allowed"
            >
              Restart
            </button>
            <button
              onClick={() => handleAction(serviceAPI.enable)}
              disabled={loading || status?.enabled}
              className="px-4 py-2 bg-blue-600 text-white rounded-md hover:bg-blue-700 disabled:opacity-50 disabled:cursor-not-allowed"
            >
              Enable Autostart
            </button>
            <button
              onClick={() => handleAction(serviceAPI.disable)}
              disabled={loading || !status?.enabled}
              className="px-4 py-2 bg-gray-600 text-white rounded-md hover:bg-gray-700 disabled:opacity-50 disabled:cursor-not-allowed"
            >
              Disable Autostart
            </button>
          </div>
        </div>

        {/* sing-box Widget */}
        <SingboxWidget />
      </div>
    </div>
  );
}
