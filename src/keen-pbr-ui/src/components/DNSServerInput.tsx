import { DNS, DNSType } from '../types';

interface DNSServerInputProps {
  value?: DNS;
  onChange: (dns: DNS | undefined) => void;
  label: string;
  allowEmpty?: boolean;
}

export function DNSServerInput({ value, onChange, label, allowEmpty = false }: DNSServerInputProps) {
  const handleTypeChange = (type: DNSType) => {
    onChange({
      type,
      server: value?.server || '',
      port: value?.port || getDefaultPort(type),
      path: type === 'https' ? (value?.path || '/dns-query') : undefined,
      throughOutbound: value?.throughOutbound ?? true,
    });
  };

  const handleServerChange = (server: string) => {
    if (!value) return;
    onChange({
      ...value,
      server,
    });
  };

  const handlePortChange = (port: string) => {
    if (!value) return;
    const portNum = parseInt(port, 10);
    if (!isNaN(portNum)) {
      onChange({
        ...value,
        port: portNum,
      });
    }
  };

  const handlePathChange = (path: string) => {
    if (!value) return;
    onChange({
      ...value,
      path,
    });
  };

  const handleThroughOutboundChange = (throughOutbound: boolean) => {
    if (!value) return;
    onChange({
      ...value,
      throughOutbound,
    });
  };

  const handleClear = () => {
    onChange(undefined);
  };

  const getDefaultPort = (type: DNSType): number => {
    switch (type) {
      case 'udp':
        return 53;
      case 'tls':
        return 853;
      case 'https':
        return 443;
    }
  };

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between">
        <label className="block text-sm font-medium text-gray-700">{label}</label>
        {allowEmpty && value && (
          <button
            type="button"
            onClick={handleClear}
            className="text-xs text-red-600 hover:text-red-700"
          >
            Clear
          </button>
        )}
      </div>

      {/* DNS Type Button Group */}
      <div className="flex gap-2">
        <button
          type="button"
          onClick={() => handleTypeChange('udp')}
          className={`px-4 py-2 text-sm font-medium rounded-md transition-colors ${
            value?.type === 'udp'
              ? 'bg-blue-600 text-white'
              : 'bg-gray-100 text-gray-700 hover:bg-gray-200'
          }`}
        >
          UDP
        </button>
        <button
          type="button"
          onClick={() => handleTypeChange('tls')}
          className={`px-4 py-2 text-sm font-medium rounded-md transition-colors ${
            value?.type === 'tls'
              ? 'bg-blue-600 text-white'
              : 'bg-gray-100 text-gray-700 hover:bg-gray-200'
          }`}
        >
          DoT
        </button>
        <button
          type="button"
          onClick={() => handleTypeChange('https')}
          className={`px-4 py-2 text-sm font-medium rounded-md transition-colors ${
            value?.type === 'https'
              ? 'bg-blue-600 text-white'
              : 'bg-gray-100 text-gray-700 hover:bg-gray-200'
          }`}
        >
          DoH
        </button>
      </div>

      {/* DNS Server Details */}
      {value && (
        <div className="space-y-3 p-4 bg-gray-50 rounded-md">
          <div>
            <label className="block text-sm font-medium text-gray-700 mb-1">
              Server
            </label>
            <input
              type="text"
              value={value.server}
              onChange={(e) => handleServerChange(e.target.value)}
              placeholder="8.8.8.8 or dns.google"
              className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          <div>
            <label className="block text-sm font-medium text-gray-700 mb-1">
              Port
            </label>
            <input
              type="number"
              value={value.port}
              onChange={(e) => handlePortChange(e.target.value)}
              min="1"
              max="65535"
              className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>

          {value.type === 'https' && (
            <div>
              <label className="block text-sm font-medium text-gray-700 mb-1">
                Path
              </label>
              <input
                type="text"
                value={value.path || ''}
                onChange={(e) => handlePathChange(e.target.value)}
                placeholder="/dns-query"
                className="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          )}

          <div className="flex items-center">
            <input
              type="checkbox"
              id={`${label}-through-outbound`}
              checked={value.throughOutbound ?? true}
              onChange={(e) => handleThroughOutboundChange(e.target.checked)}
              className="h-4 w-4 text-blue-600 focus:ring-blue-500 border-gray-300 rounded"
            />
            <label
              htmlFor={`${label}-through-outbound`}
              className="ml-2 block text-sm text-gray-700"
            >
              Query through outbound
            </label>
          </div>
        </div>
      )}

      {!value && allowEmpty && (
        <div className="p-4 bg-gray-50 rounded-md text-sm text-gray-500 text-center">
          Click a DNS type button above to configure
        </div>
      )}
    </div>
  );
}
