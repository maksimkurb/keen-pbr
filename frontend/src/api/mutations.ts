import { useQueryClient } from "@tanstack/react-query"

import {
  postConfig,
  postReload,
  postRoutingTest,
  usePostConfig,
  usePostReload,
  usePostRoutingTest,
} from "@/api/generated/keen-api"
import {
  invalidationKeysAfterConfigMutation,
  invalidationKeysAfterReloadMutation,
} from "@/api/query-keys"

type UsePostConfigOptions = Parameters<typeof usePostConfig>[0]
type UsePostReloadOptions = Parameters<typeof usePostReload>[0]
type UsePostRoutingTestOptions = Parameters<typeof usePostRoutingTest>[0]

export { postConfig, postReload, postRoutingTest }

export const usePostConfigMutation = (options?: UsePostConfigOptions) => {
  const queryClient = useQueryClient()

  return usePostConfig({
    ...options,
    mutation: {
      ...options?.mutation,
      onSuccess: async (data, variables, context) => {
        for (const queryKey of invalidationKeysAfterConfigMutation) {
          await queryClient.invalidateQueries({ queryKey })
        }

        await options?.mutation?.onSuccess?.(data, variables, context)
      },
    },
  })
}

export const usePostReloadMutation = (options?: UsePostReloadOptions) => {
  const queryClient = useQueryClient()

  return usePostReload({
    ...options,
    mutation: {
      ...options?.mutation,
      onSuccess: async (data, variables, context) => {
        for (const queryKey of invalidationKeysAfterReloadMutation) {
          await queryClient.invalidateQueries({ queryKey })
        }

        await options?.mutation?.onSuccess?.(data, variables, context)
      },
    },
  })
}

export const usePostRoutingTestMutation = (options?: UsePostRoutingTestOptions) => usePostRoutingTest(options)
