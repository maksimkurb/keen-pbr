import { DNS, DNSType } from '../types';
import { ToggleButtonGroup } from './ui/ToggleButtonGroup';
import { Input } from './ui/Input';
import { Checkbox } from './ui/Checkbox';

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
      port: getDefaultPort(type), // Always use default port when type changes
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
        {allowEmpty && value && value.server.trim() !== '' && (
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
      <ToggleButtonGroup
        options={[
          { value: 'udp' as DNSType, label: 'UDP' },
          { value: 'tls' as DNSType, label: 'DoT' },
          { value: 'https' as DNSType, label: 'DoH' },
        ]}
        value={value?.type || 'udp'}
        onChange={handleTypeChange}
      />

      {/* DNS Server Details */}
      {value && (
        <div className="space-y-3 p-4 bg-gray-50 rounded-md">
          <Input
            label="Server"
            type="text"
            value={value.server}
            onChange={(e) => handleServerChange(e.target.value)}
            placeholder="8.8.8.8 or dns.google"
          />

          <Input
            label="Port"
            type="number"
            value={value.port.toString()}
            onChange={(e) => handlePortChange(e.target.value)}
            min="1"
            max="65535"
          />

          {value.type === 'https' && (
            <Input
              label="Path"
              type="text"
              value={value.path || ''}
              onChange={(e) => handlePathChange(e.target.value)}
              placeholder="/dns-query"
            />
          )}

          <Checkbox
            id={`${label}-through-outbound`}
            label="Query through outbound"
            checked={value.throughOutbound ?? true}
            onChange={(e) => handleThroughOutboundChange(e.target.checked)}
          />
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
