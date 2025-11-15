import { useEffect, useState } from 'react';
import { singboxAPI, type SingboxBinaryStatus, type SingboxProcessInfo } from '../api/client';

export default function SingboxWidget() {
  const [status, setStatus] = useState<SingboxBinaryStatus | null>(null);
  const [processInfo, setProcessInfo] = useState<SingboxProcessInfo | null>(null);
  const [downloading, setDownloading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const loadStatus = async () => {
    try {
      setError(null);
      const [binaryStatus, runtimeStatus] = await Promise.all([
        singboxAPI.getVersion(),
        singboxAPI.getStatus(),
      ]);
      setStatus(binaryStatus);
      setProcessInfo(runtimeStatus);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load sing-box status');
    }
  };

  useEffect(() => {
    loadStatus();
    // Refresh status every 5 seconds to catch crashes quickly
    const interval = setInterval(loadStatus, 5000);
    return () => clearInterval(interval);
  }, []);

  const handleDownload = async () => {
    setDownloading(true);
    setError(null);
    try {
      await singboxAPI.download();
      // Reload status after download
      await loadStatus();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to download sing-box');
    } finally {
      setDownloading(false);
    }
  };

  const getStatusColor = (isWorking: boolean) => {
    return isWorking ? 'bg-green-100 text-green-800' : 'bg-red-100 text-red-800';
  };

  const getProcessStatusColor = (processStatus: string) => {
    switch (processStatus) {
      case 'running':
        return 'bg-green-100 text-green-800';
      case 'stopped':
        return 'bg-gray-100 text-gray-800';
      case 'crashed':
      case 'failed':
        return 'bg-red-100 text-red-800';
      default:
        return 'bg-gray-100 text-gray-800';
    }
  };

  return (
    <div className="bg-white shadow rounded-lg p-6">
      <h2 className="text-2xl font-bold text-gray-900 mb-6">sing-box Status</h2>

      {error && (
        <div className="mb-4 p-4 bg-red-50 border border-red-200 rounded-md">
          <p className="text-red-800">{error}</p>
        </div>
      )}

      {status && (
        <div className="space-y-4">
          {/* Binary Status */}
          <div className="flex items-center space-x-4">
            <span className="text-gray-700 font-medium">Binary Status:</span>
            <span className={`px-3 py-1 rounded-full text-sm font-medium ${getStatusColor(status.isWorking)}`}>
              {status.exists ? (status.isWorking ? 'WORKING' : 'NOT WORKING') : 'NOT INSTALLED'}
            </span>
          </div>

          {/* Runtime Status */}
          {processInfo && (
            <div className="flex items-center space-x-4">
              <span className="text-gray-700 font-medium">Runtime Status:</span>
              <span className={`px-3 py-1 rounded-full text-sm font-medium uppercase ${getProcessStatusColor(processInfo.status)}`}>
                {processInfo.status}
              </span>
              {processInfo.pid && (
                <span className="text-gray-500 text-sm">PID: {processInfo.pid}</span>
              )}
            </div>
          )}

          {status.exists && (
            <>
              <div className="flex items-center space-x-4">
                <span className="text-gray-700 font-medium">Path:</span>
                <span className="text-gray-600 text-sm font-mono">{status.path}</span>
              </div>

              {status.installedVersion && (
                <div className="flex items-center space-x-4">
                  <span className="text-gray-700 font-medium">Installed Version:</span>
                  <span className="text-gray-600 font-mono">{status.installedVersion}</span>
                </div>
              )}
            </>
          )}

          <div className="flex items-center space-x-4">
            <span className="text-gray-700 font-medium">Configured Version:</span>
            <span className="text-gray-600 font-mono">{status.configuredVersion}</span>
          </div>

          {/* Runtime Error Output */}
          {processInfo?.errorOutput && (
            <div className="p-4 bg-red-50 border border-red-200 rounded-md">
              <h3 className="text-red-800 font-semibold mb-2">sing-box Error:</h3>
              <pre className="text-red-700 text-xs font-mono whitespace-pre-wrap overflow-x-auto max-h-64 overflow-y-auto">
                {processInfo.errorOutput}
              </pre>
            </div>
          )}

          {/* Binary Error */}
          {status.error && !processInfo?.errorOutput && (
            <div className="p-3 bg-yellow-50 border border-yellow-200 rounded-md">
              <p className="text-yellow-800 text-sm">{status.error}</p>
            </div>
          )}

          {(!status.exists || !status.isWorking) && (
            <button
              onClick={handleDownload}
              disabled={downloading}
              className="px-4 py-2 bg-blue-600 text-white rounded-md hover:bg-blue-700 disabled:opacity-50 disabled:cursor-not-allowed"
            >
              {downloading ? 'Downloading...' : 'Download sing-box'}
            </button>
          )}
        </div>
      )}

      {!status && !error && (
        <div className="text-gray-500">Loading...</div>
      )}
    </div>
  );
}
