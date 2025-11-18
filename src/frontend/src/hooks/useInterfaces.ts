import { useQuery } from '@tanstack/react-query';
import { apiClient } from '../api/client';

/**
 * Hook for fetching network interfaces
 * Not cached - fetches fresh data when combobox is opened
 */
export function useInterfaces(enabled = true) {
  return useQuery({
    queryKey: ['interfaces'],
    queryFn: () => apiClient.getInterfaces(),
    enabled,
    staleTime: 0, // Always fetch fresh data
    refetchOnMount: true, // Refetch when component mounts
  });
}
