I want you to plan new project named keen-pbr in @src/

It should be:
1. `src/keen-pbr` is a golang project that is a web-server with JSON configuration and API.
2. `src/keen-pbr-ui` is a react project (SPA) with tailwind, rspack that is communitating with keen-pbr API

keen-pbr is a golang utility for policy-based routing, so I want to have the following API endpoints:
1. `/v1/service/...`: start, stop, restart, enable, disable, status - controlling keen-pbr status
2. `/v1/rules/...`: CRUD with Rule entities
3. `/v1/outbound-tables/`: CRUD with OutboundTable entities
4. `/v1/config`: CRUD with Config entity

Entities:
```
record Rule {
  DNS[] customDnsServers
  List[] lists
  Outbound outbound
}

record DNS {
  DNSType type
  String server
  uint16 port
  String path
  bool throughOutbound = true // should we send DNS requests through outbound or directly to our ISP?
}

enum DNSType {
  UDP,
  TLS,
  HTTPS
}

interface List {
  ListType type
}

record LocalList extends List {
  ListType type = "local"
  String path
}

record RemoteList extends List {
  ListType type = "remote"
  String url
  Duration updateInterval
}

enum ListType {
  REMOTE,
  LOCAL
}

enum ListFormat {
  SOURCE,
  BINARY
}

interface OutboundTable {
  OutboundTableType type
}

interface StaticOutboundTable extends OutboundTable {
  OutboundTableType type = "static"
  String outbound // outbound tag
}

interface UrlTestOutboundTable extends OutboundTable {
  OutboundTableType type = "urltest"
  String[] outbounds // outbound tags
  String testUrl
}

enum OutboundTableType {
  STATIC,  // only single outbound will be used for these URLs
  URLTEST // multiple outbounds can be used, selected based on URL Test (they are tested periodically)
}

interface Outbound {
  String tag
  OutboundType type
}

record InterfaceOutbound extends Outbound {
  OutboundType type = "interface"
  String ifname
}

record ProxyOutbound extends Outbound {
  OutboundType type = "proxy"
  String url
}
```
