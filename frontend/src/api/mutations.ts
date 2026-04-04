import {
  useIsMutating,
  useMutation,
  useQueryClient,
} from "@tanstack/react-query"

import {
  postConfig,
  postConfigSave,
  postRoutingTest,
  usePostConfig,
  usePostConfigSave,
  usePostRoutingTest,
} from "@/api/generated/keen-api"
import {
  invalidationKeysAfterApplyConfigMutation,
  invalidationKeysAfterConfigMutation,
  invalidationKeysAfterRuntimeActionMutation,
} from "@/api/query-keys"
import { apiFetch } from "@/api/client"

type UsePostConfigOptions = Parameters<typeof usePostConfig>[0]
type UsePostConfigSaveOptions = Parameters<typeof usePostConfigSave>[0]
type UsePostRoutingTestOptions = Parameters<typeof usePostRoutingTest>[0]

export { postConfig, postConfigSave, postRoutingTest }

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

export const useApplyConfigMutation = (options?: UsePostConfigSaveOptions) => {
  const queryClient = useQueryClient()

  return usePostConfigSave({
    ...options,
    mutation: {
      ...options?.mutation,
      onSuccess: async (data, variables, onMutateResult, context) => {
        for (const queryKey of invalidationKeysAfterApplyConfigMutation) {
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

export const usePostRoutingTestMutation = (
  options?: UsePostRoutingTestOptions
) => usePostRoutingTest(options)

type ServiceAction = "start" | "stop" | "restart"
const serviceActionMutationKey = (action: ServiceAction) =>
  ["serviceAction", action] as const

const postServiceAction = (action: ServiceAction) =>
  apiFetch(`/api/service/${action}`, {
    method: "POST",
  })

export const usePostServiceActionMutation = (action: ServiceAction) => {
  const queryClient = useQueryClient()

  return useMutation({
    mutationKey: serviceActionMutationKey(action),
    mutationFn: () => postServiceAction(action),
    onSuccess: async () => {
      for (const queryKey of invalidationKeysAfterRuntimeActionMutation) {
        await queryClient.invalidateQueries({ queryKey })
      }
    },
  })
}

export const useRoutingControlPendingState = () => {
  const applyPending = useIsMutating({ mutationKey: ["postConfigSave"] }) > 0
  const startPending =
    useIsMutating({ mutationKey: serviceActionMutationKey("start") }) > 0
  const stopPending =
    useIsMutating({ mutationKey: serviceActionMutationKey("stop") }) > 0
  const restartPending =
    useIsMutating({ mutationKey: serviceActionMutationKey("restart") }) > 0

  return {
    applyPending,
    startPending,
    stopPending,
    restartPending,
    anyPending: applyPending || startPending || stopPending || restartPending,
  }
}
