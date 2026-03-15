import { useQueryClient } from "@tanstack/react-query"

import {
  postConfig,
  postConfigSave,
  postReload,
  postRoutingTest,
  usePostConfig,
  usePostConfigSave,
  usePostReload,
  usePostRoutingTest,
} from "@/api/generated/keen-api"
import {
  invalidationKeysAfterConfigMutation,
  invalidationKeysAfterConfigSaveMutation,
  invalidationKeysAfterReloadMutation,
} from "@/api/query-keys"

type UsePostConfigOptions = Parameters<typeof usePostConfig>[0]
type UsePostConfigSaveOptions = Parameters<typeof usePostConfigSave>[0]
type UsePostReloadOptions = Parameters<typeof usePostReload>[0]
type UsePostRoutingTestOptions = Parameters<typeof usePostRoutingTest>[0]

export { postConfig, postConfigSave, postReload, postRoutingTest }

export const usePostConfigMutation = (options?: UsePostConfigOptions) => {
  const queryClient = useQueryClient()

  return usePostConfig({
    ...options,
    mutation: {
      ...options?.mutation,
      onSuccess: async (data, variables, onMutateResult, context) => {
        for (const queryKey of invalidationKeysAfterConfigMutation) {
          await queryClient.invalidateQueries({ queryKey })
        }

        await options?.mutation?.onSuccess?.(
          data,
          variables,
          onMutateResult,
          context
        )
      },
    },
  })
}

export const usePostConfigSaveMutation = (options?: UsePostConfigSaveOptions) => {
  const queryClient = useQueryClient()

  return usePostConfigSave({
    ...options,
    mutation: {
      ...options?.mutation,
      onSuccess: async (data, variables, onMutateResult, context) => {
        for (const queryKey of invalidationKeysAfterConfigSaveMutation) {
          await queryClient.invalidateQueries({ queryKey })
        }

        await options?.mutation?.onSuccess?.(
          data,
          variables,
          onMutateResult,
          context
        )
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
      onSuccess: async (data, variables, onMutateResult, context) => {
        for (const queryKey of invalidationKeysAfterReloadMutation) {
          await queryClient.invalidateQueries({ queryKey })
        }

        await options?.mutation?.onSuccess?.(
          data,
          variables,
          onMutateResult,
          context
        )
      },
    },
  })
}

export const usePostRoutingTestMutation = (options?: UsePostRoutingTestOptions) => usePostRoutingTest(options)
