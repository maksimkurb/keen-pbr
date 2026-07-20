import type { RuntimeInterfaceInventoryEntry } from "@/api/generated/model"

export function getInterfaceSearchText(
  interfaceEntry?: RuntimeInterfaceInventoryEntry
) {
  if (!interfaceEntry) {
    return ""
  }

  return [
    interfaceEntry.name,
    interfaceEntry.description,
    ...(interfaceEntry.ipv4_addresses ?? []),
    ...(interfaceEntry.ipv6_addresses ?? []),
  ].join(" ")
}
