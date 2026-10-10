import { useRef, useState } from "react"

import { usePostRoutingTestMutation } from "@/api/mutations"
import { useGetRuntimeOutbounds } from "@/api/queries"
import type { RoutingTestRequest } from "@/api/generated/model"

import { initialRoutingTestCriteria } from "./routing-test-criteria"

export function useRoutingTestPanelState() {
  const [testTarget, setTestTarget] = useState("")
  const [criteria, setCriteria] = useState(initialRoutingTestCriteria)
  const lastRequestRef = useRef<RoutingTestRequest | null>(null)
  const [routingInputError, setRoutingInputError] = useState<string | null>(
    null
  )

  const routingTestMutation = usePostRoutingTestMutation()
  const runtimeOutboundsQuery = useGetRuntimeOutbounds()
  const routingDiagnostics =
    routingTestMutation.data?.status === 200
      ? routingTestMutation.data.data
      : undefined

  return {
    testTarget,
    setTestTarget,
    criteria,
    setCriteria,
    lastRequestRef,
    routingInputError,
    setRoutingInputError,
    routingTestMutation,
    runtimeOutboundsQuery,
    routingDiagnostics,
  }
}
