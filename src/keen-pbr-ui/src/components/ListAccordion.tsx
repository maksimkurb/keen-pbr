import { useState } from 'react';
import type { List, ListFormat } from '../types';

interface ListAccordionProps {
  list: List;
  index: number;
  onUpdate: (updates: List) => void;
  onDelete: () => void;
}

export default function ListAccordion({ list, index, onUpdate, onDelete }: ListAccordionProps) {
  const [isExpanded, setIsExpanded] = useState(false);

  const getListTitle = () => {
    const typeLabel = list.type.charAt(0).toUpperCase() + list.type.slice(1);
    return `${typeLabel} List`;
  };

  const getLastUpdateText = () => {
    if (list.type === 'remote' && list.lastUpdate) {
      const date = new Date(list.lastUpdate);
      return ` • Last updated: ${date.toLocaleString()}`;
    }
    return '';
  };

  return (
    <div className="border border-gray-300 rounded-md">
      {/* Accordion Header */}
      <button
        type="button"
        onClick={() => setIsExpanded(!isExpanded)}
        className="w-full px-4 py-3 flex items-center justify-between text-left hover:bg-gray-50 transition-colors"
      >
        <div className="flex-1">
          <span className="font-medium text-gray-900">{getListTitle()}</span>
          <span className="text-sm text-gray-500">{getLastUpdateText()}</span>
        </div>
        <div className="flex items-center gap-2">
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation();
              onDelete();
            }}
            className="px-2 py-1 text-sm text-red-600 hover:text-red-800 transition-colors"
          >
            Delete
          </button>
          <svg
            className={`w-5 h-5 text-gray-500 transition-transform ${isExpanded ? 'transform rotate-180' : ''}`}
            fill="none"
            stroke="currentColor"
            viewBox="0 0 24 24"
          >
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 9l-7 7-7-7" />
          </svg>
        </div>
      </button>

      {/* Accordion Content */}
      {isExpanded && (
        <div className="px-4 py-3 border-t border-gray-200 space-y-3">
          {list.type === 'inline' && (
            <>
              <div>
                <label htmlFor={`list-entries-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                  Domains/IPs (one per line)
                </label>
                <textarea
                  id={`list-entries-${index}`}
                  value={list.entries.join('\n')}
                  onChange={(e) =>
                    onUpdate({
                      type: 'inline',
                      entries: e.target.value.split('\n'),
                    })
                  }
                  onKeyDown={(e) => {
                    // Allow Enter key to create newlines without submitting form
                    if (e.key === 'Enter') {
                      e.stopPropagation();
                    }
                  }}
                  rows={10}
                  placeholder="example.com&#10;192.168.1.1&#10;*.google.com"
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500 font-mono text-sm"
                />
                <p className="mt-1 text-xs text-gray-500">
                  Enter one domain or IP address per line. Supports wildcards (*.example.com).
                </p>
              </div>
            </>
          )}

          {list.type === 'local' && (
            <>
              <div>
                <label htmlFor={`list-path-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                  File Path
                </label>
                <input
                  id={`list-path-${index}`}
                  type="text"
                  value={list.path}
                  onChange={(e) =>
                    onUpdate({
                      ...list,
                      path: e.target.value,
                    })
                  }
                  placeholder="/etc/keen-pbr/lists/domains.txt"
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                />
              </div>
              <div>
                <label htmlFor={`list-format-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                  Format
                </label>
                <select
                  id={`list-format-${index}`}
                  value={list.format}
                  onChange={(e) =>
                    onUpdate({
                      ...list,
                      format: e.target.value as ListFormat,
                    })
                  }
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                >
                  <option value="source">Source (plain text)</option>
                  <option value="binary">Binary (sing-box SRS)</option>
                </select>
                <p className="mt-1 text-xs text-gray-500">
                  Source format: plain text file with one entry per line. Binary format: sing-box SRS compiled format.
                </p>
              </div>
            </>
          )}

          {list.type === 'remote' && (
            <>
              <div>
                <label htmlFor={`list-url-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                  URL
                </label>
                <input
                  id={`list-url-${index}`}
                  type="text"
                  value={list.url}
                  onChange={(e) =>
                    onUpdate({
                      ...list,
                      url: e.target.value,
                    })
                  }
                  placeholder="https://example.com/lists/domains.txt"
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                />
              </div>
              <div>
                <label htmlFor={`list-interval-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                  Update Interval
                </label>
                <input
                  id={`list-interval-${index}`}
                  type="text"
                  value={list.updateInterval}
                  onChange={(e) =>
                    onUpdate({
                      ...list,
                      updateInterval: e.target.value,
                    })
                  }
                  placeholder="1h, 30m, 24h"
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                />
                <p className="mt-1 text-xs text-gray-500">
                  Duration format: 1h (1 hour), 30m (30 minutes), 24h (24 hours), etc.
                </p>
              </div>
              <div>
                <label htmlFor={`list-remote-format-${index}`} className="block text-sm font-medium text-gray-700 mb-1">
                  Format
                </label>
                <select
                  id={`list-remote-format-${index}`}
                  value={list.format}
                  onChange={(e) =>
                    onUpdate({
                      ...list,
                      format: e.target.value as ListFormat,
                    })
                  }
                  className="w-full px-3 py-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
                >
                  <option value="source">Source (plain text)</option>
                  <option value="binary">Binary (sing-box SRS)</option>
                </select>
                <p className="mt-1 text-xs text-gray-500">
                  Source format: plain text file with one entry per line. Binary format: sing-box SRS compiled format.
                </p>
              </div>
            </>
          )}
        </div>
      )}
    </div>
  );
}
