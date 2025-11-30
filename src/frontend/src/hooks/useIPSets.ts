import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { apiClient, type IPSetConfig } from '../api/client';

/**
 * Hook for fetching all IPSets
 */
export function useIPSets() {
  return useQuery({
    queryKey: ['ipsets'],
    queryFn: () => apiClient.getIPSets(),
  });
}

/**
 * Hook for fetching a specific IPSet
 */
export function useIPSet(name: string) {
  return useQuery({
    queryKey: ['ipsets', name],
    queryFn: () => apiClient.getIPSet(name),
    enabled: !!name,
  });
}

/**
 * Hook for creating a new IPSet
 */
export function useCreateIPSet() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (data: IPSetConfig) => apiClient.createIPSet(data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['ipsets'] });
    },
  });
}

/**
 * Hook for updating an existing IPSet
 */
export function useUpdateIPSet() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      name,
      data,
    }: {
      name: string;
      data: Partial<Omit<IPSetConfig, 'ipset_name'>>;
    }) => apiClient.updateIPSet(name, data),
    onSuccess: (_, variables) => {
      queryClient.invalidateQueries({ queryKey: ['ipsets'] });
      queryClient.invalidateQueries({ queryKey: ['ipsets', variables.name] });
    },
  });
}

/**
 * Hook for deleting an IPSet
 */
export function useDeleteIPSet() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (name: string) => apiClient.deleteIPSet(name),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['ipsets'] });
    },
  });
}
