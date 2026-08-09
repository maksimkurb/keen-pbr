import { authenticatedFetch } from "@/api/client"

export type SseMessage = { event: string; data: string }

export async function consumeAuthenticatedSse(
  url: string,
  signal: AbortSignal,
  onMessage: (message: SseMessage) => void
) {
  const response = await authenticatedFetch(url, {
    signal,
  })
  if (!response.ok || !response.body)
    throw new Error(`SSE request failed (${response.status})`)

  const reader = response.body.getReader()
  const decoder = new TextDecoder()
  let buffer = ""
  while (!signal.aborted) {
    const { done, value } = await reader.read()
    if (done) break
    buffer += decoder.decode(value, { stream: true }).replace(/\r\n/g, "\n")
    let boundary = buffer.indexOf("\n\n")
    while (boundary >= 0) {
      const frame = buffer.slice(0, boundary)
      buffer = buffer.slice(boundary + 2)
      let event = "message"
      const data: string[] = []
      for (const line of frame.split("\n")) {
        if (line.startsWith("event:")) event = line.slice(6).trim()
        if (line.startsWith("data:")) data.push(line.slice(5).trimStart())
      }
      if (data.length) onMessage({ event, data: data.join("\n") })
      boundary = buffer.indexOf("\n\n")
    }
  }
}
