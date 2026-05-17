export type DnsCheckEvent =
  | { type: "HELLO" }
  | { type: "DNS"; domain?: string | null; source_ip?: string | null; ecs?: string | null }

export function parseDnsCheckEvent(data: string): DnsCheckEvent | null {
  if (!data.trim()) {
    return null
  }

  try {
    const parsed = JSON.parse(data) as DnsCheckEvent
    if (!parsed || typeof parsed !== "object" || !("type" in parsed)) {
      return null
    }
    return parsed
  } catch {
    return null
  }
}
