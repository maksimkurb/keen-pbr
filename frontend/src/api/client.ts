export type ApiError = {
  status: number
  message: string
  details?: unknown
}

const parseResponsePayload = async (response: Response) => {
  const contentType = response.headers.get("content-type") ?? ""
  if (contentType.includes("application/json")) {
    return response.json()
  }

  return response.text()
}

const normalizeError = (status: number, payload: unknown): ApiError => {
  if (payload && typeof payload === "object") {
    const body = payload as Record<string, unknown>
    const message =
      typeof body.error === "string"
        ? body.error
        : typeof body.message === "string"
          ? body.message
          : `Request failed with status ${status}`

    return { status, message, details: payload }
  }

  if (typeof payload === "string" && payload.length > 0) {
    return { status, message: payload, details: payload }
  }

  return { status, message: `Request failed with status ${status}`, details: payload }
}

const API_FETCH_TIMEOUT_MS = 90_000

function composeFetchSignals(
  userSignal: AbortSignal | undefined | null,
  timeoutMs: number,
): AbortSignal | undefined {
  if (
    typeof AbortSignal === "undefined" ||
    typeof AbortSignal.timeout !== "function"
  ) {
    return userSignal ?? undefined
  }

  const timeoutSig = AbortSignal.timeout(timeoutMs)
  const AbortWithAny = AbortSignal as typeof AbortSignal & {
    any?: (signals: AbortSignal[]) => AbortSignal
  }

  if (userSignal && typeof AbortWithAny.any === "function") {
    return AbortWithAny.any([userSignal, timeoutSig])
  }

  return userSignal ?? timeoutSig
}

export const apiFetch = async <T>(url: string, options: RequestInit): Promise<T> => {
  const response = await fetch(url, {
    ...options,
    signal: composeFetchSignals(options.signal, API_FETCH_TIMEOUT_MS),
  })
  const payload = await parseResponsePayload(response)

  if (!response.ok) {
    throw normalizeError(response.status, payload)
  }

  return {
    data: payload,
    status: response.status,
    headers: response.headers,
  } as T
}
