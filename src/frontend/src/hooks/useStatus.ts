import { useQuery } from '@tanstack/react-query';
import { apiClient } from '../api/client';

/**
 * Hook for fetching system status (services, version, config)
 * Automatically refetches every 5 seconds
 * Shared across components via react-query cache
 */
export function useStatus() {
  return useQuery({
    queryKey: ['status'],
    queryFn: () => apiClient.getStatus(),
    refetchInterval: 5000, // Refresh every 5 seconds
  });
}
