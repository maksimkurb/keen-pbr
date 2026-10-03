import type { ConfigObject } from "@/api/generated/model/configObject"

export function buildUpdatedConfigForDnsServersDelete(
  config: ConfigObject,
  serverTags: Iterable<string>
): ConfigObject {
  const tagSet = new Set(serverTags)
  const dnsConfig = config.dns

  return {
    ...config,
    dns: {
      ...(dnsConfig ?? {}),
      servers: (dnsConfig?.servers ?? []).filter(
        (server) => !tagSet.has(server.tag)
      ),
    },
  }
}
