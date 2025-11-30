import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiClient, type GeneralConfig } from '../api/client';

/**
 * Hook for fetching general settings
 */
export function useSettings() {
  return useQuery({
    queryKey: ['settings'],
    queryFn: () => apiClient.getSettings(),
  });
}

/**
 * Hook for updating general settings
 */
export function useUpdateSettings() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (data: Partial<GeneralConfig>) => apiClient.updateSettings(data),
    onSuccess: (data) => {
      // Update the cache with the new data
      queryClient.setQueryData(['settings'], data);
      // Also invalidate to ensure we're in sync
      queryClient.invalidateQueries({ queryKey: ['settings'] });
    },
  });
}
