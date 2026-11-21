export type ApiError = {
  status: number
  message: string
  details?: unknown
}

// The message the daemon sent with a failed request, or the fallback.
export const apiErrorMessage = (error: unknown, fallback: string): string => {
  if (error && typeof error === "object") {
    const message = (error as { message?: unknown }).message
    if (typeof message === "string" && message.length > 0) return message
  }
  return fallback
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

  return {
    status,
    message: `Request failed with status ${status}`,
    details: payload,
  }
}

export const authenticatedFetch = async (
  url: string,
  options: RequestInit = {}
): Promise<Response> => {
  const token = sessionStorage.getItem("keen-pbr-auth-token")
  const headers = new Headers(options.headers)
  if (token) headers.set("X-Auth-Token", token)

  const response = await fetch(url, { ...options, headers })
  if (response.status === 401) {
    sessionStorage.removeItem("keen-pbr-auth-token")
    window.dispatchEvent(new Event("keen-pbr-auth-required"))
  }
  return response
}

export const apiFetch = async <T>(
  url: string,
  options: RequestInit
): Promise<T> => {
  const response = await authenticatedFetch(url, options)
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
