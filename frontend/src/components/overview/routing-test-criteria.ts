import type { RoutingTestRequest } from "@/api/generated/model"

export type RoutingTestCriteria = {
  proto: "tcp" | "udp" | "other"
  dest_port: string
  src_addr: string
  src_port: string
  dscp: string
}

export const initialRoutingTestCriteria: RoutingTestCriteria = {
  proto: "tcp",
  dest_port: "443",
  src_addr: "",
  src_port: "",
  dscp: "",
}

export function buildRoutingTestRequest(
  target: string,
  criteria: RoutingTestCriteria
): RoutingTestRequest {
  return {
    target,
    proto: criteria.proto,
    ...(criteria.proto !== "other" && criteria.dest_port.trim()
      ? { dest_port: Number(criteria.dest_port) }
      : {}),
    ...(criteria.src_addr.trim() ? { src_addr: criteria.src_addr.trim() } : {}),
    ...(criteria.proto !== "other" && criteria.src_port.trim()
      ? { src_port: Number(criteria.src_port) }
      : {}),
    ...(criteria.dscp.trim() ? { dscp: Number(criteria.dscp) } : {}),
  }
}
