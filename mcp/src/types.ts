import type { components } from "./generated/api-types.js";

/**
 * Aliases for the OpenAPI component schemas. The file is generated from
 * docs/openapi.yaml with `npm run api:generate`; never edit it by hand.
 */
type Schemas = components["schemas"];

export type HealthResponse = Schemas["HealthResponse"];
export type RoutingHealthResponse = Schemas["RoutingHealthResponse"];
export type HealthIssuesResponse = Schemas["HealthIssuesResponse"];
export type HealthIssue = Schemas["HealthIssue"];
export type KernelCheck = Schemas["KernelCheck"];
export type RuntimeOutboundsResponse = Schemas["RuntimeOutboundsResponse"];
export type RuntimeOutboundState = Schemas["RuntimeOutboundState"];
export type RuntimeInterfaceInventoryResponse = Schemas["RuntimeInterfaceInventoryResponse"];
export type RuntimeInterfaceInventoryEntry = Schemas["RuntimeInterfaceInventoryEntry"];
export type RoutingTestRequest = Schemas["RoutingTestRequest"];
export type RoutingTestResponse = Schemas["RoutingTestResponse"];
export type DnsCheckRequest = Schemas["DnsCheckRequest"];
export type DnsCheckResponse = Schemas["DnsCheckResponse"];
export type DraftConfig = Schemas["DraftConfig"];
export type DaemonConfig = Schemas["DaemonConfig"];
export type Outbound = Schemas["Outbound"];
export type ListConfig = Schemas["ListConfig"];
export type DnsConfig = Schemas["DnsConfig"];
export type DnsRule = Schemas["DnsRule"];
export type RouteConfig = Schemas["RouteConfig"];
export type RouteRule = Schemas["RouteRule"];
export type ConfigStateResponse = Schemas["ConfigStateResponse"];
export type ConfigUpdateResponse = Schemas["ConfigUpdateResponse"];
export type ListRefreshResponse = Schemas["ListRefreshResponse"];
export type LifecycleOperationAcceptedResponse = Schemas["LifecycleOperationAcceptedResponse"];
export type ErrorResponse = Schemas["ErrorResponse"];
export type ValidationError = Schemas["ValidationError"];
export type RoutingHealthWarning = Schemas["RoutingHealthWarning"];
export type RoutingHealthWarningCode = Schemas["RoutingHealthWarningCode"];
export type HealthIssueCode = Schemas["HealthIssueCode"];

/** Events observed on the DNS test SSE stream (payloads are protocol data). */
export type DnsStreamEvent = {
  event?: string;
  data: unknown;
};

export type JsonObject = Record<string, unknown>;
