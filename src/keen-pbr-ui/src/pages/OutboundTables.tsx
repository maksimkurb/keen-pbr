import { useEffect, useState } from 'react';
import { outboundTablesAPI } from '../api/client';
import type { OutboundTable } from '../types';

export default function OutboundTables() {
  const [tables, setTables] = useState<Record<string, OutboundTable>>({});
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const loadTables = async () => {
    try {
      setError(null);
      const data = await outboundTablesAPI.getAll();
      setTables(data);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load outbound tables');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadTables();
  }, []);

  const handleDelete = async (id: string) => {
    if (!confirm('Are you sure you want to delete this outbound table?')) return;

    try {
      await outboundTablesAPI.delete(id);
      await loadTables();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to delete outbound table');
    }
  };

  if (loading) {
    return <div className="px-4 py-6">Loading...</div>;
  }

  return (
    <div className="px-4 py-6 sm:px-0">
      <div className="bg-white shadow rounded-lg">
        <div className="px-6 py-4 border-b border-gray-200 flex justify-between items-center">
          <h2 className="text-2xl font-bold text-gray-900">Outbound Tables</h2>
          <button className="px-4 py-2 bg-blue-600 text-white rounded-md hover:bg-blue-700">
            Add Table
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
                  Type
                </th>
                <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">
                  Details
                </th>
                <th className="px-6 py-3 text-right text-xs font-medium text-gray-500 uppercase tracking-wider">
                  Actions
                </th>
              </tr>
            </thead>
            <tbody className="bg-white divide-y divide-gray-200">
              {Object.entries(tables).map(([id, table]) => (
                <tr key={id}>
                  <td className="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900">
                    {id}
                  </td>
                  <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-500">
                    {table.type}
                  </td>
                  <td className="px-6 py-4 text-sm text-gray-500">
                    {table.type === 'static' ? (
                      <span>Outbound: {table.outbound}</span>
                    ) : (
                      <span>
                        {table.outbounds.length} outbound(s), Test URL: {table.testUrl}
                      </span>
                    )}
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

          {Object.keys(tables).length === 0 && (
            <div className="px-6 py-12 text-center text-gray-500">
              No outbound tables configured yet. Click "Add Table" to create one.
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
