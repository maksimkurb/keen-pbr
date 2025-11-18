import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiClient, type ListInfo, type CreateListRequest } from '../api/client';

/**
 * Hook for fetching all lists
 */
export function useLists() {
  return useQuery({
    queryKey: ['lists'],
    queryFn: () => apiClient.getLists(),
  });
}

/**
 * Hook for fetching a specific list
 */
export function useList(name: string) {
  return useQuery({
    queryKey: ['lists', name],
    queryFn: () => apiClient.getList(name),
    enabled: !!name,
  });
}

/**
 * Hook for creating a new list
 */
export function useCreateList() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (data: CreateListRequest) => apiClient.createList(data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['lists'] });
    },
  });
}

/**
 * Hook for updating an existing list
 */
export function useUpdateList() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({ name, data }: { name: string; data: CreateListRequest }) =>
      apiClient.updateList(name, data),
    onSuccess: (_, variables) => {
      queryClient.invalidateQueries({ queryKey: ['lists'] });
      queryClient.invalidateQueries({ queryKey: ['lists', variables.name] });
    },
  });
}

/**
 * Hook for deleting a list
 */
export function useDeleteList() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (name: string) => apiClient.deleteList(name),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['lists'] });
    },
  });
}
