import type { NetworkInterface, Outbound, OutboundType } from '../types';

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
        <div>
          <label htmlFor={`tag-${entry.tag}`} className="block text-sm font-medium text-gray-700 mb-1">
            Tag
          </label>
          <input
            id={`tag-${entry.tag}`}
            type="text"
            value={entry.tag}
            onChange={(e) => onTagChange(e.target.value)}
            disabled={!entry.isNew}
            placeholder="e.g., proxy-us"
            className={`w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500 ${
              !entry.isNew ? 'bg-gray-100 text-gray-600' : ''
            }`}
          />
          <p className="mt-1 text-xs text-gray-500">
            {entry.isNew ? 'Unique identifier for this outbound' : 'Tag cannot be changed after creation'}
          </p>
        </div>

        {/* Type selector */}
        <div>
          <label className="block text-sm font-medium text-gray-700 mb-2">
            Type
          </label>
          <div className="inline-flex rounded-md shadow-sm" role="group">
            <button
              type="button"
              onClick={() => onTypeChange('interface')}
              className={`px-4 py-2 text-sm font-medium border rounded-l-lg ${
                entry.data.type === 'interface'
                  ? 'bg-blue-600 text-white border-blue-600'
                  : 'bg-white text-gray-700 border-gray-300 hover:bg-gray-50'
              }`}
            >
              Interface
            </button>
            <button
              type="button"
              onClick={() => onTypeChange('proxy')}
              className={`px-4 py-2 text-sm font-medium border rounded-r-lg ${
                entry.data.type === 'proxy'
                  ? 'bg-blue-600 text-white border-blue-600'
                  : 'bg-white text-gray-700 border-gray-300 hover:bg-gray-50'
              }`}
            >
              Proxy
            </button>
          </div>
        </div>

        {/* Conditional fields based on type */}
        {entry.data.type === 'interface' ? (
          <div>
            <label htmlFor={`ifname-${entry.tag}`} className="block text-sm font-medium text-gray-700 mb-1">
              Network Interface
            </label>
            <select
              id={`ifname-${entry.tag}`}
              value={entry.data.ifname}
              onChange={(e) =>
                onDataChange({
                  tag: entry.tag,
                  type: 'interface',
                  ifname: e.target.value,
                })
              }
              className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
            >
              <option value="">Select interface...</option>
              {interfaces.map((iface) => (
                <option key={iface.name} value={iface.name}>
                  {iface.name} ({iface.ips.join(', ')}) {iface.isUp ? '✓' : '✗'}
                </option>
              ))}
            </select>
            <p className="mt-1 text-xs text-gray-500">
              ✓ = Up, ✗ = Down
            </p>
          </div>
        ) : (
          <div>
            <label htmlFor={`url-${entry.tag}`} className="block text-sm font-medium text-gray-700 mb-1">
              Proxy URL
            </label>
            <input
              id={`url-${entry.tag}`}
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
              className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
            />
            <p className="mt-1 text-xs text-gray-500">
              sing-box format (e.g., vless://, vmess://, trojan://, etc.)
            </p>
          </div>
        )}

        {/* Actions */}
        <div className="flex justify-end space-x-2 pt-2">
          {entry.isNew ? (
            <button
              onClick={onRemove}
              className="px-3 py-1.5 text-sm text-red-600 hover:text-red-800 transition-colors"
            >
              Remove
            </button>
          ) : (
            <button
              onClick={onDelete}
              className="px-3 py-1.5 text-sm text-red-600 hover:text-red-800 transition-colors"
            >
              Delete
            </button>
          )}
        </div>
      </div>
    </div>
  );
}
