import type { NetworkInterface, Outbound, OutboundType } from '../types';
import { ToggleButtonGroup } from './ui/ToggleButtonGroup';
import { Input } from './ui/Input';
import { Select } from './ui/Select';
import { Button } from './ui/Button';

interface OutboundEntry {
  tag: string;
  data: Outbound;
  isNew: boolean;
}

interface OutboundCardProps {
  entry: OutboundEntry;
  interfaces: NetworkInterface[];
  onTagChange: (tag: string) => void;
  onTypeChange: (type: OutboundType) => void;
  onDataChange: (newData: Outbound) => void;
  onRemove: () => void;
  onDelete: () => void;
}

export default function OutboundCard({
  entry,
  interfaces,
  onTagChange,
  onTypeChange,
  onDataChange,
  onRemove,
  onDelete,
}: OutboundCardProps) {
  return (
    <div className="bg-white shadow rounded-lg p-6">
      <div className="space-y-4">
        {/* Tag field */}
        <Input
          id={`tag-${entry.tag}`}
          label="Tag"
          type="text"
          value={entry.tag}
          onChange={(e) => onTagChange(e.target.value)}
          disabled={!entry.isNew}
          placeholder="e.g., proxy-us"
          helperText={entry.isNew ? 'Unique identifier for this outbound' : 'Tag cannot be changed after creation'}
        />

        {/* Type selector */}
        <div>
          <label className="block text-sm font-medium text-gray-700 mb-2">
            Type
          </label>
          <ToggleButtonGroup
            options={[
              { value: 'interface' as OutboundType, label: 'Interface' },
              { value: 'proxy' as OutboundType, label: 'Proxy' },
            ]}
            value={entry.data.type}
            onChange={onTypeChange}
          />
        </div>

        {/* Conditional fields based on type */}
        {entry.data.type === 'interface' ? (
          <Select
            id={`ifname-${entry.tag}`}
            label="Network Interface"
            value={entry.data.ifname}
            onChange={(e) =>
              onDataChange({
                tag: entry.tag,
                type: 'interface',
                ifname: e.target.value,
              })
            }
            helperText="✓ = Up, ✗ = Down"
          >
            <option value="">Select interface...</option>
            {interfaces.map((iface) => (
              <option key={iface.name} value={iface.name}>
                {iface.name} ({iface.ips.join(', ')}) {iface.isUp ? '✓' : '✗'}
              </option>
            ))}
          </Select>
        ) : (
          <Input
            id={`url-${entry.tag}`}
            label="Proxy URL"
            type="text"
            value={entry.data.url || ''}
            onChange={(e) =>
              onDataChange({
                tag: entry.tag,
                type: 'proxy',
                url: e.target.value,
              })
            }
            placeholder="vless://uuid@server:port?type=tcp&security=tls#name"
            helperText="sing-box format (e.g., vless://, vmess://, trojan://, etc.)"
          />
        )}

        {/* Actions */}
        <div className="flex justify-end space-x-2 pt-2">
          {entry.isNew ? (
            <Button onClick={onRemove} variant="ghost" size="sm" className="text-red-600 hover:text-red-800">
              Remove
            </Button>
          ) : (
            <Button onClick={onDelete} variant="ghost" size="sm" className="text-red-600 hover:text-red-800">
              Delete
            </Button>
          )}
        </div>
      </div>
    </div>
  );
}
