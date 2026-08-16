import {
  useIsMutating,
  useMutation,
  useQueryClient,
} from "@tanstack/react-query"

import {
  postListsRefresh,
  postConfig,
  postConfigDiscard,
  postConfigRollback,
  postConfigSave,
  postRoutingTest,
  usePostListsRefresh,
  usePostConfig,
  usePostConfigDiscard,
  usePostConfigRollback,
  usePostConfigSave,
  usePostRoutingTest,
} from "@/api/generated/keen-api"
import {
  invalidationKeysAfterApplyConfigMutation,
  invalidationKeysAfterConfigMutation,
  invalidationKeysAfterListRefreshMutation,
  invalidationKeysAfterRuntimeActionMutation,
} from "@/api/query-keys"
import { apiFetch } from "@/api/client"

type UsePostListsRefreshOptions = Parameters<typeof usePostListsRefresh>[0]
type UsePostConfigOptions = Parameters<typeof usePostConfig>[0]
type UsePostConfigDiscardOptions = Parameters<typeof usePostConfigDiscard>[0]
type UsePostConfigRollbackOptions = Parameters<typeof usePostConfigRollback>[0]
type UsePostConfigSaveOptions = Parameters<typeof usePostConfigSave>[0]
type UsePostRoutingTestOptions = Parameters<typeof usePostRoutingTest>[0]

export {
  postConfig,
  postConfigDiscard,
  postConfigRollback,
  postConfigSave,
  postListsRefresh,
  postRoutingTest,
}

export const usePostListsRefreshMutation = (
  options?: UsePostListsRefreshOptions
) => {
  const queryClient = useQueryClient()

  return usePostListsRefresh({
    ...options,
    mutation: {
      ...options?.mutation,
      onSuccess: async (data, variables, onMutateResult, context) => {
        for (const queryKey of invalidationKeysAfterListRefreshMutation) {
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

export const useDiscardConfigMutation = (
  options?: UsePostConfigDiscardOptions
) => {
  const queryClient = useQueryClient()

  return usePostConfigDiscard({
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

export const useRollbackConfigMutation = (
  options?: UsePostConfigRollbackOptions
) => {
  const queryClient = useQueryClient()
  return usePostConfigRollback({
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

export function isConfigMutationPending(
  postConfigCount: number,
  postConfigSaveCount: number,
  postConfigDiscardCount = 0
) {
  return (
    postConfigCount > 0 || postConfigSaveCount > 0 || postConfigDiscardCount > 0
  )
}

export const useConfigMutationPending = () => {
  const postConfigCount = useIsMutating({ mutationKey: ["postConfig"] })
  const postConfigSaveCount = useIsMutating({ mutationKey: ["postConfigSave"] })
  const postConfigDiscardCount = useIsMutating({
    mutationKey: ["postConfigDiscard"],
  })

  return isConfigMutationPending(
    postConfigCount,
    postConfigSaveCount,
    postConfigDiscardCount
  )
}

export const useRoutingControlPendingState = () => {
  const draftPostPending = useIsMutating({ mutationKey: ["postConfig"] }) > 0
  const applyPending = useIsMutating({ mutationKey: ["postConfigSave"] }) > 0
  const discardPending =
    useIsMutating({ mutationKey: ["postConfigDiscard"] }) > 0
  const rollbackPending =
    useIsMutating({ mutationKey: ["postConfigRollback"] }) > 0
  const configMutationPending =
    draftPostPending || applyPending || discardPending || rollbackPending
  const startPending =
    useIsMutating({ mutationKey: serviceActionMutationKey("start") }) > 0
  const stopPending =
    useIsMutating({ mutationKey: serviceActionMutationKey("stop") }) > 0
  const restartPending =
    useIsMutating({ mutationKey: serviceActionMutationKey("restart") }) > 0

  return {
    applyPending,
    discardPending,
    rollbackPending,
    draftPostPending,
    configMutationPending,
    startPending,
    stopPending,
    restartPending,
    anyPending:
      configMutationPending || startPending || stopPending || restartPending,
  }
}
