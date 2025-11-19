import { useQuery } from '@tanstack/react-query';
import { apiClient } from '../api/client';

/**
 * Hook for fetching network interfaces
 * Cached for 5 seconds and auto-refreshes every 5 seconds when enabled
 */
export function useInterfaces(enabled = true) {
  return useQuery({
    queryKey: ['interfaces'],
    queryFn: () => apiClient.getInterfaces(),
    enabled,
    staleTime: 5000, // Cache for 5 seconds
    refetchInterval: enabled ? 5000 : false, // Auto-refresh every 5 seconds when enabled
    refetchOnMount: true, // Refetch when component mounts
  });
}
