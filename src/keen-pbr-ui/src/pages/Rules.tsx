import { useEffect, useState } from 'react';
import { rulesAPI } from '../api/client';
import type { Rule } from '../types';

export default function Rules() {
  const [rules, setRules] = useState<Record<string, Rule>>({});
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const loadRules = async () => {
    try {
      setError(null);
      const data = await rulesAPI.getAll();
      setRules(data);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load rules');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadRules();
  }, []);

  const handleDelete = async (id: string) => {
    if (!confirm('Are you sure you want to delete this rule?')) return;

    try {
      await rulesAPI.delete(id);
      await loadRules();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to delete rule');
    }
  };

  if (loading) {
    return <div className="px-4 py-6">Loading...</div>;
  }

  return (
    <div className="px-4 py-6 sm:px-0">
      <div className="bg-white shadow rounded-lg">
        <div className="px-6 py-4 border-b border-gray-200 flex justify-between items-center">
          <h2 className="text-2xl font-bold text-gray-900">Rules</h2>
          <button className="px-4 py-2 bg-blue-600 text-white rounded-md hover:bg-blue-700">
            Add Rule
          </button>
        </div>

        {error && (
          <div className="m-6 p-4 bg-red-50 border border-red-200 rounded-md">
            <p className="text-red-800">{error}</p>
          </div>
        )}

        <div className="overflow-x-auto">
          <table className="min-w-full divide-y divide-gray-200">
            <thead className="bg-gray-50">
              <tr>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                  ID
                </th>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                  Outbound
                </th>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                  Lists
                </th>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                  DNS Servers
                </th>
                <th className="px-6 py-3 text-right text-xs font-medium text-gray-500 uppercase tracking-wider">
                  Actions
                </th>
              </tr>
            </thead>
            <tbody className="bg-white divide-y divide-gray-200">
              {Object.entries(rules).map(([id, rule]) => (
                <tr key={id}>
                  <td className="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                    {id}
                  </td>
                  <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-500">
                    {rule.outbound.tag} ({rule.outbound.type})
                  </td>
                  <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-500">
                    {rule.lists.length} list(s)
                  </td>
                  <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-500">
                    {rule.customDnsServers?.length || 0} server(s)
                  </td>
                  <td className="px-6 py-4 whitespace-nowrap text-right text-sm font-medium">
                    <button className="text-blue-600 hover:text-blue-900 mr-4">
                      Edit
                    </button>
                    <button
                      onClick={() => handleDelete(id)}
                      className="text-red-600 hover:text-red-900"
                    >
                      Delete
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>

          {Object.keys(rules).length === 0 && (
            <div className="px-6 py-12 text-center text-gray-500">
              No rules configured yet. Click "Add Rule" to create one.
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
