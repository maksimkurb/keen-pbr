import type { InterceptProbeFeatureStatus } from "@/api/generated/model"

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
