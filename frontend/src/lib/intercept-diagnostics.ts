import type { InterceptProbeFeatureStatus } from "@/api/generated/model"

export type KernelEntryKind = "capability" | "probe"

export type KernelDiagnosticEntry = {
  key: string
  kind: KernelEntryKind
  feature: string
  status: string
  relevant: boolean
  reason?: string
  /** A missing capability that has a fallback: a warning, never an error. */
  advisory?: boolean
}

export type KernelBadgeState = "healthy" | "neutral" | "degraded"

/** ok and skipped need no attention; not_run is unknown and stays visible. */
export function isHealthyInterceptStatus(status: string): boolean {
  return status === "ok" || status === "skipped"
}

export function getVisibleInterceptDiagnosticEntries<
  T extends { relevant: boolean; status: string },
>(entries: ReadonlyArray<T>, showHealthyEntries: boolean): T[] {
  return entries.filter(
    (entry) =>
      entry.relevant &&
      (showHealthyEntries || !isHealthyInterceptStatus(entry.status))
  )
}

/** Maps capabilities and probes into the entry shape of the kernel group. */
export function mapKernelDiagnosticEntries(
  capabilities: {
    nfqueue: boolean
    nflog: boolean
    connbytes: boolean
    addrtype?: boolean
  },
  probes: ReadonlyArray<{
    feature: string
    status: InterceptProbeFeatureStatus
    reason?: string
  }>,
  requestedDns: boolean,
  requestedL7: boolean
): KernelDiagnosticEntry[] {
  const capabilityEntries: KernelDiagnosticEntry[] = [
    ["nfqueue", capabilities.nfqueue, requestedDns],
    ["nflog", capabilities.nflog, requestedL7],
    ["connbytes", capabilities.connbytes, requestedL7],
  ].map(([name, supported, relevant]) => ({
    key: `capability:${name as string}`,
    kind: "capability",
    feature: name as string,
    status: supported ? "ok" : "unsupported",
    relevant: relevant as boolean,
  }))
  // addrtype serves the router-output skip rules (always built), and has a
  // `-d` address-match fallback, so it only ever warns.
  if (capabilities.addrtype !== undefined) {
    capabilityEntries.push({
      key: "capability:addrtype",
      kind: "capability",
      feature: "addrtype",
      status: capabilities.addrtype ? "ok" : "unsupported",
      relevant: true,
      advisory: true,
    })
  }
  const probeEntries: KernelDiagnosticEntry[] = probes.map((probe) => ({
    key: `probe:${probe.feature}`,
    kind: "probe",
    feature: probe.feature,
    status: probe.status,
    relevant:
      (probe.feature === "nfqueue" && requestedDns) ||
      ((probe.feature === "nflog" || probe.feature === "connbytes") &&
        requestedL7) ||
      ((requestedDns || requestedL7) &&
        !["nfqueue", "nflog", "connbytes"].includes(probe.feature)),
    reason: probe.reason,
  }))
  return [...capabilityEntries, ...probeEntries]
}

/**
 * Kernel badge: red on any relevant error/unsupported (the same entries
 * collectInterceptDiagnosticErrors reports as errors), grey while some checks
 * have not run, green when everything is ok or skipped.
 */
export function getKernelBadgeState(
  entries: ReadonlyArray<{
    relevant: boolean
    status: string
    advisory?: boolean
  }>
): KernelBadgeState {
  const relevant = entries.filter((entry) => entry.relevant)
  if (
    relevant.some(
      (entry) =>
        !entry.advisory &&
        (entry.status === "error" || entry.status === "unsupported")
    )
  ) {
    return "degraded"
  }
  if (relevant.some((entry) => !isHealthyInterceptStatus(entry.status))) {
    return "neutral"
  }
  return "healthy"
}

export function collectInterceptDiagnosticErrors(
  capabilities: {
    nfqueue: boolean
    nflog: boolean
    connbytes: boolean
  },
  probes: ReadonlyArray<{
    feature: string
    status: InterceptProbeFeatureStatus
    reason?: string
  }>,
  requestedDns: boolean,
  requestedL7: boolean
): string[] {
  const errors: string[] = []
  if (requestedDns && !capabilities.nfqueue) errors.push("NFQUEUE")
  if (requestedL7 && !capabilities.nflog) errors.push("NFLOG")
  if (requestedL7 && !capabilities.connbytes) errors.push("connbytes")

  const relevant = (feature: string) =>
    (feature === "nfqueue" && requestedDns) ||
    ((feature === "nflog" || feature === "connbytes") && requestedL7) ||
    (requestedDns || requestedL7) &&
      !["nfqueue", "nflog", "connbytes"].includes(feature)
  for (const probe of probes) {
    if (
      relevant(probe.feature) &&
      (probe.status === "unsupported" || probe.status === "error") &&
      !errors.some((error) => error.startsWith(`${probe.feature}:`)) &&
      !errors.includes(
        probe.feature === "nfqueue"
          ? "NFQUEUE"
          : probe.feature === "nflog"
            ? "NFLOG"
            : probe.feature
      )
    ) {
      errors.push(`${probe.feature}: ${probe.reason ?? probe.status}`)
    }
  }
  return errors
}
