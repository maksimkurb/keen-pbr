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
export type ListType = 'inline' | 'local' | 'remote';
export type ListFormat = 'source' | 'binary';

export interface InlineList {
  type: 'inline';
  entries: string[];
}

export interface LocalList {
  type: 'local';
  path: string;
  format: ListFormat;
}

export interface RemoteList {
  type: 'remote';
  url: string;
  updateInterval: string; // Duration as string (e.g., "1h", "30m")
  format: ListFormat;
  lastUpdate?: string; // ISO 8601 datetime string
}

export type List = InlineList | LocalList | RemoteList;

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
  name?: string;
  enabled: boolean;
  priority: number;
  customDnsServers?: DNS[];
  lists: List[];
  outboundTable: OutboundTable;
}

// General Settings Type
export interface GeneralSettings {
  defaultDnsServer?: DNS;
  bootstrapDnsServer?: DNS;
  inboundInterfaces?: string[];
}

// Config Type
export interface Config {
  rules: Record<string, Rule>;
  outbounds: Record<string, Outbound>;
  generalSettings?: GeneralSettings;
}

// Network Interface Type
export interface NetworkInterface {
  name: string;
  ips: string[];
  isUp: boolean;
}

// Service Status Types
export type ServiceStatus = 'stopped' | 'running' | 'starting' | 'stopping';

export interface ServiceStatusResponse {
  status: ServiceStatus;
  enabled: boolean;
}
