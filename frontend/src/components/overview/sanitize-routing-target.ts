export function sanitizeRoutingTarget(input: string): string | null {
  const trimmed = input.trim()
  if (!trimmed) {
    return null
  }

  try {
    const parsed = new URL(trimmed)
    if (parsed.hostname) {
      return parsed.hostname
    }
  } catch {
    // fall through
  }

  const domainPattern =
    /^([a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\.)*[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?$/
  const ipv4Pattern = /^(\d{1,3}\.){3}\d{1,3}$/
  const ipv6Pattern =
    /^(([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|::1|::|[0-9a-fA-F:]+)$/

  if (domainPattern.test(trimmed) || ipv4Pattern.test(trimmed) || ipv6Pattern.test(trimmed)) {
    return trimmed
  }

  return null
}
