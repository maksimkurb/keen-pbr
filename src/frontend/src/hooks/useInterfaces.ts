import { useQuery } from '@tanstack/react-query';
import { apiClient } from '../api/client';

/**
 * Hook for fetching network interfaces
 * Cached for 5 seconds to provide fresh data
 */
export function useInterfaces(enabled = true) {
  return useQuery({
    queryKey: ['interfaces'],
    queryFn: () => apiClient.getInterfaces(),
    enabled,
    staleTime: 5000, // Cache for 5 seconds
    refetchOnMount: true, // Refetch when component mounts
  });
}
