import { useEffect, useState } from 'react';
import { infoAPI, outboundsAPI } from '../api/client';
import type { NetworkInterface, Outbound, OutboundType } from '../types';
import OutboundCard from '../components/OutboundCard';
import { Button } from '../components/ui/Button';

interface OutboundEntry {
  tag: string;
  data: Outbound;
  isNew: boolean;
}

export default function Outbounds() {
  const [interfaces, setInterfaces] = useState<NetworkInterface[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [entries, setEntries] = useState<OutboundEntry[]>([]);

  const loadData = async () => {
    try {
      setError(null);
      const [outboundsData, interfacesData] = await Promise.all([
        outboundsAPI.getAll(),
        infoAPI.getInterfaces(),
      ]);
      setInterfaces(interfacesData);

      // Convert outbounds to entries
      const outboundEntries = Object.entries(outboundsData).map(([tag, data]) => ({
        tag,
        data,
        isNew: false,
      }));
      setEntries(outboundEntries);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load data');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, []);

  const handleAddOutbound = () => {
    const newEntry: OutboundEntry = {
      tag: '',
      data: {
        tag: '',
        type: 'interface',
        ifname: '',
      },
      isNew: true,
    };
    setEntries([newEntry, ...entries]);
  };

  const handleSave = async () => {
    try {
      setError(null);

      // Validate all entries
      for (const entry of entries) {
        if (!entry.tag.trim()) {
          setError('All outbounds must have a tag');
          return;
        }
        if (entry.data.type === 'interface' && !entry.data.ifname) {
          setError(`Outbound "${entry.tag}" must have an interface selected`);
          return;
        }
        if (entry.data.type === 'proxy' && !entry.data.url) {
          setError(`Outbound "${entry.tag}" must have a proxy URL`);
          return;
        }
      }

      // Convert entries to outbounds array
      const outbounds = entries.map(entry => entry.data);

      // Bulk update all outbounds
      await outboundsAPI.bulkUpdate(outbounds);

      await loadData();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to save outbounds');
    }
  };

  const handleDelete = (index: number) => {
    if (confirm(`Are you sure you want to delete outbound "${entries[index].tag}"?`)) {
      removeEntry(index);
    }
  };

  const handleTagChange = (index: number, tag: string) => {
    setEntries(entries.map((entry, i) => {
      if (i !== index) return entry;
      // Update both entry.tag and entry.data.tag to keep them in sync
      return {
        ...entry,
        tag,
        data: { ...entry.data, tag },
      };
    }));
  };

  const updateEntryData = (index: number, newData: Outbound) => {
    setEntries(entries.map((entry, i) => {
      if (i !== index) return entry;
      return { ...entry, data: newData };
    }));
  };

  const handleTypeChange = (index: number, type: OutboundType) => {
    const entry = entries[index];
    if (type === 'interface') {
      updateEntryData(index, {
        tag: entry.tag,
        type: 'interface',
        ifname: interfaces[0]?.name || '',
      });
    } else {
      updateEntryData(index, {
        tag: entry.tag,
        type: 'proxy',
        url: '',
      });
    }
  };

  const removeEntry = (index: number) => {
    setEntries(entries.filter((_, i) => i !== index));
  };

  if (loading) {
    return <div className="px-4 py-6">Loading...</div>;
  }

  return (
    <div className="px-4 py-6 sm:px-0">
      <div className="mb-6 flex justify-between items-center">
        <h1 className="text-3xl font-bold text-gray-900">Outbounds</h1>
        <Button onClick={handleAddOutbound}>
          Add Outbound
        </Button>
      </div>

      {error && (
        <div className="mb-6 p-4 bg-red-50 border border-red-200 rounded-md">
          <p className="text-red-800">{error}</p>
        </div>
      )}

      <div className="space-y-4">
        {entries.map((entry, index) => (
          <OutboundCard
            key={index}
            entry={entry}
            interfaces={interfaces}
            onTagChange={(tag) => handleTagChange(index, tag)}
            onTypeChange={(type) => handleTypeChange(index, type)}
            onDataChange={(updates) => updateEntryData(index, updates)}
            onRemove={() => removeEntry(index)}
            onDelete={() => handleDelete(index)}
          />
        ))}
      </div>

      {entries.length === 0 && (
        <div className="py-12 text-center text-gray-500">
          No outbounds configured yet. Click "Add Outbound" to create one.
        </div>
      )}

      {entries.length > 0 && (
        <div className="mt-6">
          <Button onClick={handleSave} size="lg">
            Save
          </Button>
        </div>
      )}
    </div>
  );
}
