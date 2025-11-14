// DNS Types
export type DNSType = 'udp' | 'tls' | 'https';

export interface DNS {
  type: DNSType;
  server: string;
  port: number;
  path?: string;
  throughOutbound?: boolean;
}

// List Types
export type ListType = 'local' | 'remote';

export interface LocalList {
  type: 'local';
  path: string;
}

export interface RemoteList {
  type: 'remote';
  url: string;
  updateInterval: string; // Duration as string (e.g., "1h", "30m")
}

export type List = LocalList | RemoteList;

// Outbound Types
export type OutboundType = 'interface' | 'proxy';

export interface InterfaceOutbound {
  tag: string;
  type: 'interface';
  ifname: string;
}

export interface ProxyOutbound {
  tag: string;
  type: 'proxy';
  url: string;
}

export type Outbound = InterfaceOutbound | ProxyOutbound;

// Outbound Table Types
export type OutboundTableType = 'static' | 'urltest';

export interface StaticOutboundTable {
  type: 'static';
  outbound: string;
}

export interface URLTestOutboundTable {
  type: 'urltest';
  outbounds: string[];
  testUrl: string;
}

export type OutboundTable = StaticOutboundTable | URLTestOutboundTable;

// Rule Type
export interface Rule {
  id?: string;
  customDnsServers?: DNS[];
  lists: List[];
  outbound: Outbound;
}

// Config Type
export interface Config {
  rules: Record<string, Rule>;
  outboundTables: Record<string, OutboundTable>;
  outbounds: Record<string, Outbound>;
}

// Service Status Types
export type ServiceStatus = 'stopped' | 'running' | 'starting' | 'stopping';

export interface ServiceStatusResponse {
  status: ServiceStatus;
  enabled: boolean;
}
