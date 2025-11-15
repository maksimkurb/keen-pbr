import { useState, useEffect } from 'react';
import type { NetworkInterface } from '../types';
import { Select } from './ui/Select';
import { Button } from './ui/Button';
import { infoAPI } from '../api/client';

interface InterfaceSelectorProps {
  selectedInterfaces: string[];
  onChange: (interfaces: string[]) => void;
  label?: string;
  helperText?: string;
}

export function InterfaceSelector({
  selectedInterfaces,
  onChange,
  label = 'Network Interfaces',
  helperText,
}: InterfaceSelectorProps) {
  const [interfaces, setInterfaces] = useState<NetworkInterface[]>([]);
  const [selectedInterface, setSelectedInterface] = useState<string>('');
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    loadInterfaces();
  }, []);

  const loadInterfaces = async () => {
    try {
      setLoading(true);
      const data = await infoAPI.getInterfaces();
      setInterfaces(data);
    } catch (err) {
      console.error('Failed to load interfaces:', err);
    } finally {
      setLoading(false);
    }
  };

  const handleAdd = () => {
    if (selectedInterface && !selectedInterfaces.includes(selectedInterface)) {
      onChange([...selectedInterfaces, selectedInterface]);
      setSelectedInterface('');
    }
  };

  const handleRemove = (iface: string) => {
    onChange(selectedInterfaces.filter((i) => i !== iface));
  };

  // Get available interfaces that haven't been selected yet
  const availableInterfaces = interfaces.filter(
    (iface) => !selectedInterfaces.includes(iface.name)
  );

  return (
    <div>
      <label className="block text-sm font-medium text-gray-700 mb-2">
        {label}
      </label>
      {helperText && (
        <p className="text-sm text-gray-500 mb-3">{helperText}</p>
      )}

      {/* List of selected interfaces */}
      <div className="space-y-2 mb-3">
        {selectedInterfaces.length === 0 ? (
          <p className="text-sm text-gray-400 italic">
            No interfaces configured (will default to br0)
          </p>
        ) : (
          selectedInterfaces.map((iface) => {
            const ifaceInfo = interfaces.find((i) => i.name === iface);
            return (
              <div key={iface} className="flex items-center gap-2">
                <div className="flex-1 px-3 py-2 border border-gray-300 rounded-md bg-gray-50 text-gray-700">
                  {iface}
                  {ifaceInfo && (
                    <span className="text-gray-500 text-sm ml-2">
                      ({ifaceInfo.ips.join(', ')}) {ifaceInfo.isUp ? '✓' : '✗'}
                    </span>
                  )}
                </div>
                <Button
                  onClick={() => handleRemove(iface)}
                  variant="ghost"
                  size="sm"
                  className="text-red-600 hover:text-red-800"
                >
                  Remove
                </Button>
              </div>
            );
          })
        )}
      </div>

      {/* Add interface selector */}
      <div className="flex items-center gap-2">
        <Select
          id="interface-selector"
          value={selectedInterface}
          onChange={(e) => setSelectedInterface(e.target.value)}
          disabled={loading || availableInterfaces.length === 0}
          className="flex-1"
        >
          <option value="">
            {loading
              ? 'Loading interfaces...'
              : availableInterfaces.length === 0
              ? 'No interfaces available'
              : 'Select interface...'}
          </option>
          {availableInterfaces.map((iface) => (
            <option key={iface.name} value={iface.name}>
              {iface.name} ({iface.ips.join(', ')}) {iface.isUp ? '✓' : '✗'}
            </option>
          ))}
        </Select>
        <Button
          onClick={handleAdd}
          variant="primary"
          size="sm"
          disabled={!selectedInterface || loading}
        >
          Add
        </Button>
      </div>
    </div>
  );
}
