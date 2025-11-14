import { useEffect, useState } from 'react';
import { rulesAPI, outboundsAPI } from '../api/client';
import type { Rule, OutboundTable, List, DNS } from '../types';
import ListAccordion from '../components/ListAccordion';
import { DNSServerInput } from '../components/DNSServerInput';

interface RuleEntry {
  id: string;
  data: Rule;
  isNew: boolean;
}

export default function Rules() {
  const [entries, setEntries] = useState<RuleEntry[]>([]);
  const [availableOutbounds, setAvailableOutbounds] = useState<string[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const loadData = async () => {
    try {
      setError(null);
      const [rulesData, outboundsData] = await Promise.all([
        rulesAPI.getAll(),
        outboundsAPI.getAll(),
      ]);

      // Convert rules to entries
      const ruleEntries = Object.entries(rulesData).map(([id, data]) => ({
        id,
        data,
        isNew: false,
      }));
      setEntries(ruleEntries);

      // Get available outbound tags
      setAvailableOutbounds(Object.keys(outboundsData));
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load data');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadData();
  }, []);

  const handleAddRule = () => {
    const newEntry: RuleEntry = {
      id: '',
      data: {
        name: '',
        enabled: true,
        priority: 0,
        lists: [],
        outboundTable: {
          type: 'static',
          outbound: availableOutbounds[0] || '',
        },
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
        if (!entry.id.trim()) {
          setError('All rules must have an ID');
          return;
        }
        if (entry.data.outboundTable.type === 'static' && !entry.data.outboundTable.outbound) {
          setError(`Rule "${entry.id}" must have an outbound selected`);
          return;
        }
        if (entry.data.outboundTable.type === 'urltest') {
          if (entry.data.outboundTable.outbounds.length === 0) {
            setError(`Rule "${entry.id}" must have at least one outbound for URL test`);
            return;
          }
          if (!entry.data.outboundTable.testUrl) {
            setError(`Rule "${entry.id}" must have a test URL for URL test`);
            return;
          }
        }
      }

      // Convert entries to rules array
      const rules = entries.map(entry => ({
        ...entry.data,
        id: entry.id,
      }));

      // Bulk update all rules
      await rulesAPI.bulkUpdate(rules);

      await loadData();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to save rules');
    }
  };

  const handleDelete = (index: number) => {
    if (confirm(`Are you sure you want to delete rule "${entries[index].id}"?`)) {
      removeEntry(index);
    }
  };

  const handleIdChange = (index: number, id: string) => {
    setEntries(entries.map((entry, i) => (i === index ? { ...entry, id } : entry)));
  };

  const updateRuleData = (index: number, updates: Partial<Rule>) => {
    setEntries(entries.map((entry, i) => {
      if (i !== index) return entry;
      return { ...entry, data: { ...entry.data, ...updates } };
    }));
  };

  const updateOutboundTable = (index: number, outboundTable: OutboundTable) => {
    updateRuleData(index, { outboundTable });
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
        <h1 className="text-3xl font-bold text-gray-900">Rules</h1>
        <button
          onClick={handleAddRule}
          className="px-4 py-2 bg-blue-600 text-white rounded-md hover:bg-blue-700 transition-colors"
        >
          Add Rule
        </button>
      </div>

      {error && (
        <div className="mb-6 p-4 bg-red-50 border border-red-200 rounded-md">
          <p className="text-red-800">{error}</p>
        </div>
      )}

      <div className="space-y-4">
        {entries.map((entry, index) => (
          <div key={index} className="bg-white shadow rounded-lg p-6">
            <div className="space-y-4">
              {/* ID field */}
              <div>
                <label htmlFor={`id-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                  ID
                </label>
                <input
                  id={`id-${index}`}
                  type="text"
                  value={entry.id}
                  onChange={(e) => handleIdChange(index, e.target.value)}
                  disabled={!entry.isNew}
                  placeholder="e.g., rule-youtube"
                  className={`w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500 ${
                    !entry.isNew ? 'bg-gray-100 text-gray-600' : ''
                  }`}
                />
                <p className="mt-1 text-xs text-gray-500">
                  {entry.isNew ? 'Unique identifier for this rule' : 'ID cannot be changed after creation'}
                </p>
              </div>

              {/* Name field */}
              <div>
                <label htmlFor={`name-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                  Name (optional)
                </label>
                <input
                  id={`name-${index}`}
                  type="text"
                  value={entry.data.name || ''}
                  onChange={(e) => updateRuleData(index, { name: e.target.value })}
                  placeholder="e.g., YouTube Traffic"
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                />
              </div>

              {/* Enabled and Priority */}
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <label className="flex items-center">
                    <input
                      type="checkbox"
                      checked={entry.data.enabled}
                      onChange={(e) => updateRuleData(index, { enabled: e.target.checked })}
                      className="rounded border-gray-300 text-blue-600 focus:ring-blue-500"
                    />
                    <span className="ml-2 text-sm font-medium text-gray-700">Enabled</span>
                  </label>
                </div>
                <div>
                  <label htmlFor={`priority-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                    Priority
                  </label>
                  <input
                    id={`priority-${index}`}
                    type="number"
                    value={entry.data.priority}
                    onChange={(e) => updateRuleData(index, { priority: parseInt(e.target.value) || 0 })}
                    className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                  />
                </div>
              </div>

              {/* Outbound Table */}
              <div>
                <label className="block text-sm font-medium text-gray-700 mb-2">
                  Outbound Table
                </label>
                <div className="inline-flex rounded-md shadow-sm mb-3" role="group">
                  <button
                    type="button"
                    onClick={() => updateOutboundTable(index, {
                      type: 'static',
                      outbound: availableOutbounds[0] || '',
                    })}
                    className={`px-4 py-2 text-sm font-medium border rounded-l-lg ${
                      entry.data.outboundTable.type === 'static'
                        ? 'bg-blue-600 text-white border-blue-600'
                        : 'bg-white text-gray-700 border-gray-300 hover:bg-gray-50'
                    }`}
                  >
                    Static
                  </button>
                  <button
                    type="button"
                    onClick={() => updateOutboundTable(index, {
                      type: 'urltest',
                      outbounds: availableOutbounds.slice(0, 1),
                      testUrl: 'https://www.gstatic.com/generate_204',
                    })}
                    className={`px-4 py-2 text-sm font-medium border rounded-r-lg ${
                      entry.data.outboundTable.type === 'urltest'
                        ? 'bg-blue-600 text-white border-blue-600'
                        : 'bg-white text-gray-700 border-gray-300 hover:bg-gray-50'
                    }`}
                  >
                    URL Test
                  </button>
                </div>

                {entry.data.outboundTable.type === 'static' ? (
                  <div>
                    <label htmlFor={`outbound-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                      Outbound
                    </label>
                    <select
                      id={`outbound-${index}`}
                      value={entry.data.outboundTable.outbound}
                      onChange={(e) => updateOutboundTable(index, {
                        type: 'static',
                        outbound: e.target.value,
                      })}
                      className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                    >
                      <option value="">Select outbound...</option>
                      {availableOutbounds.map((tag) => (
                        <option key={tag} value={tag}>
                          {tag}
                        </option>
                      ))}
                    </select>
                  </div>
                ) : entry.data.outboundTable.type === 'urltest' ? (
                  <div className="space-y-3">
                    <div>
                      <label className="block text-sm font-medium text-gray-700 mb-1">
                        Outbounds
                      </label>
                      <div className="space-y-2">
                        {entry.data.outboundTable.outbounds.map((outbound, outboundIndex) => (
                          <div key={outboundIndex} className="flex gap-2">
                            <select
                              value={outbound}
                              onChange={(e) => {
                                if (entry.data.outboundTable.type === 'urltest') {
                                  const newOutbounds = [...entry.data.outboundTable.outbounds];
                                  newOutbounds[outboundIndex] = e.target.value;
                                  updateOutboundTable(index, {
                                    type: 'urltest',
                                    outbounds: newOutbounds,
                                    testUrl: entry.data.outboundTable.testUrl,
                                  });
                                }
                              }}
                              className="flex-1 px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                            >
                              <option value="">Select outbound...</option>
                              {availableOutbounds.map((tag) => (
                                <option key={tag} value={tag}>
                                  {tag}
                                </option>
                              ))}
                            </select>
                            <button
                              type="button"
                              onClick={() => {
                                if (entry.data.outboundTable.type === 'urltest') {
                                  const newOutbounds = entry.data.outboundTable.outbounds.filter((_: string, i: number) => i !== outboundIndex);
                                  updateOutboundTable(index, {
                                    type: 'urltest',
                                    outbounds: newOutbounds,
                                    testUrl: entry.data.outboundTable.testUrl,
                                  });
                                }
                              }}
                              className="px-3 py-2 text-sm text-red-600 hover:text-red-800 transition-colors"
                            >
                              Remove
                            </button>
                          </div>
                        ))}
                        <button
                          type="button"
                          onClick={() => {
                            if (entry.data.outboundTable.type === 'urltest') {
                              const newOutbounds = [...entry.data.outboundTable.outbounds, availableOutbounds[0] || ''];
                              updateOutboundTable(index, {
                                type: 'urltest',
                                outbounds: newOutbounds,
                                testUrl: entry.data.outboundTable.testUrl,
                              });
                            }
                          }}
                          className="px-3 py-1.5 text-sm text-blue-600 hover:text-blue-800 transition-colors"
                        >
                          + Add Outbound
                        </button>
                      </div>
                    </div>
                    <div>
                      <label htmlFor={`testUrl-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                        Test URL
                      </label>
                      <input
                        id={`testUrl-${index}`}
                        type="text"
                        value={entry.data.outboundTable.testUrl}
                        onChange={(e) => {
                          if (entry.data.outboundTable.type === 'urltest') {
                            updateOutboundTable(index, {
                              type: 'urltest',
                              outbounds: entry.data.outboundTable.outbounds,
                              testUrl: e.target.value,
                            });
                          }
                        }}
                        placeholder="https://www.gstatic.com/generate_204"
                        className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                      />
                    </div>
                  </div>
                ) : null}
              </div>

              {/* Custom DNS Server */}
              <div>
                <div className="flex items-center justify-between mb-3">
                  <label className="block text-sm font-medium text-gray-700">
                    Custom DNS Server (Optional)
                  </label>
                  {entry.data.customDnsServers && entry.data.customDnsServers.length > 0 && (
                    <button
                      type="button"
                      onClick={() => updateRuleData(index, { customDnsServers: [] })}
                      className="text-xs text-red-600 hover:text-red-700"
                    >
                      Clear DNS Server
                    </button>
                  )}
                </div>
                {(!entry.data.customDnsServers || entry.data.customDnsServers.length === 0) ? (
                  <div className="p-4 bg-gray-50 rounded-md">
                    <p className="text-sm text-gray-500 mb-3">
                      No custom DNS configured. Will use default DNS server from General Settings.
                    </p>
                    <button
                      type="button"
                      onClick={() => updateRuleData(index, {
                        customDnsServers: [{
                          type: 'udp',
                          server: '',
                          port: 53,
                          throughOutbound: true,
                        }]
                      })}
                      className="px-3 py-1.5 text-sm bg-blue-600 text-white rounded hover:bg-blue-700 transition-colors"
                    >
                      Add Custom DNS Server
                    </button>
                  </div>
                ) : (
                  <DNSServerInput
                    value={entry.data.customDnsServers[0]}
                    onChange={(dns: DNS | undefined) => {
                      if (dns) {
                        updateRuleData(index, { customDnsServers: [dns] });
                      } else {
                        updateRuleData(index, { customDnsServers: [] });
                      }
                    }}
                    label=""
                    allowEmpty={true}
                  />
                )}
              </div>

              {/* Lists section */}
              <div>
                <div className="flex items-center justify-between mb-3">
                  <label className="block text-sm font-medium text-gray-700">
                    Lists
                  </label>
                  <div className="flex gap-2">
                    <button
                      type="button"
                      onClick={() => {
                        const newLists = [...entry.data.lists, { type: 'inline' as const, entries: [] }];
                        updateRuleData(index, { lists: newLists });
                      }}
                      className="px-2 py-1 text-xs bg-blue-600 text-white rounded hover:bg-blue-700 transition-colors"
                    >
                      + Inline List
                    </button>
                    <button
                      type="button"
                      onClick={() => {
                        const newLists = [...entry.data.lists, { type: 'local' as const, path: '', format: 'source' as const }];
                        updateRuleData(index, { lists: newLists });
                      }}
                      className="px-2 py-1 text-xs bg-green-600 text-white rounded hover:bg-green-700 transition-colors"
                    >
                      + Local File List
                    </button>
                    <button
                      type="button"
                      onClick={() => {
                        const newLists = [...entry.data.lists, {
                          type: 'remote' as const,
                          url: '',
                          updateInterval: '1h',
                          format: 'source' as const
                        }];
                        updateRuleData(index, { lists: newLists });
                      }}
                      className="px-2 py-1 text-xs bg-purple-600 text-white rounded hover:bg-purple-700 transition-colors"
                    >
                      + Remote List
                    </button>
                  </div>
                </div>

                {entry.data.lists.length === 0 ? (
                  <p className="text-sm text-gray-500 text-center py-4 border border-dashed border-gray-300 rounded">
                    No lists configured. Click one of the buttons above to add a list.
                  </p>
                ) : (
                  <div className="space-y-2">
                    {entry.data.lists.map((list, listIndex) => (
                      <ListAccordion
                        key={listIndex}
                        list={list}
                        index={listIndex}
                        onUpdate={(updatedList) => {
                          const newLists = [...entry.data.lists];
                          newLists[listIndex] = updatedList;
                          updateRuleData(index, { lists: newLists });
                        }}
                        onDelete={() => {
                          const newLists = entry.data.lists.filter((_, i) => i !== listIndex);
                          updateRuleData(index, { lists: newLists });
                        }}
                      />
                    ))}
                  </div>
                )}
              </div>

              {/* Actions */}
              <div className="flex justify-end space-x-2 pt-2 border-t">
                {entry.isNew ? (
                  <button
                    onClick={() => removeEntry(index)}
                    className="px-3 py-1.5 text-sm text-red-600 hover:text-red-800 transition-colors"
                  >
                    Remove
                  </button>
                ) : (
                  <button
                    onClick={() => handleDelete(index)}
                    className="px-3 py-1.5 text-sm text-red-600 hover:text-red-800 transition-colors"
                  >
                    Delete
                  </button>
                )}
              </div>
            </div>
          </div>
        ))}
      </div>

      {entries.length === 0 && (
        <div className="py-12 text-center text-gray-500 bg-white rounded-lg shadow">
          No rules configured yet. Click "Add Rule" to create one.
        </div>
      )}

      {entries.length > 0 && (
        <div className="mt-6">
          <button
            onClick={handleSave}
            className="px-6 py-3 bg-blue-600 text-white rounded-md hover:bg-blue-700 transition-colors font-medium"
          >
            Save
          </button>
        </div>
      )}
    </div>
  );
}
