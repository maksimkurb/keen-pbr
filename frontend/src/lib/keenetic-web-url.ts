/**
 * Address of a page of the Keenetic web interface. The keen-pbr UI is served
 * by the router itself, so the page's host is the router; the keen-pbr port is
 * dropped to reach the Keenetic web interface on its default port.
 */
export function keeneticWebUrl(path: string): string {
  return `${window.location.protocol}//${window.location.hostname}${path}`
}

export const KEENETIC_DNS_SETTINGS_PATH = "/internet-filter/dns-configuration"
