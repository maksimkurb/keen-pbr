/**
 * Per-OS help for setting dnsmasq up: the package install command and the
 * documentation section that walks through it.  `os_type` comes from
 * GET /api/health/service (keenetic, openwrt, debian; anything else is a
 * generic Linux build).
 */

const SETUP_OS = ["keenetic", "openwrt", "debian"] as const
type SetupOs = (typeof SETUP_OS)[number]

const OS_LABELS: Record<SetupOs, string> = {
  keenetic: "Keenetic",
  openwrt: "OpenWrt",
  debian: "Debian",
}

function setupOs(osType?: string): SetupOs | undefined {
  return SETUP_OS.find((os) => os === osType)
}

/** "OpenWrt" for a known OS, undefined otherwise. */
export function dnsmasqSetupOsLabel(osType?: string): string | undefined {
  const os = setupOs(osType)
  return os ? OS_LABELS[os] : undefined
}

/** Shell command that installs dnsmasq; undefined when the OS is unknown. */
export function dnsmasqInstallCommand(
  osType?: string,
  osVersion?: string
): string | undefined {
  switch (setupOs(osType)) {
    case "keenetic":
      // Entware ships only dnsmasq-full.
      return "opkg install dnsmasq-full"
    case "debian":
      return "apt install dnsmasq"
    case "openwrt": {
      // OpenWrt 25 replaced opkg with apk.
      const major = Number.parseInt(osVersion ?? "", 10)
      return major >= 25 ? "apk add dnsmasq-full" : "opkg install dnsmasq-full"
    }
    default:
      return undefined
  }
}

/**
 * The dnsmasq section of the installation guide for the OS, or the generic
 * "Do I need dnsmasq?" section.  `docsBaseUrl` is the localized
 * `common.documentationUrl` (ends with "/docs/").
 */
export function dnsmasqSetupDocsUrl(
  docsBaseUrl: string,
  osType?: string
): string {
  const os = setupOs(osType)
  return os
    ? `${docsBaseUrl}getting-started/installation/${os}/#dnsmasq-install`
    : `${docsBaseUrl}configuration/dns/#do-i-need-dnsmasq`
}

/** dnsmasq errors that mean it is not installed or not set up for keen-pbr. */
const SETUP_ERROR_CODES = new Set([
  "DNSMASQ_NOT_INSTALLED",
  "DNSMASQ_NO_DNS_INSTANCE",
  "DNSMASQ_NOT_RUNNING",
  "DNSMASQ_DNS_DISABLED",
  "DNSMASQ_NO_DNS_LISTENER",
  "DNSMASQ_DROPIN_NOT_LOADED",
  "DNSMASQ_CONF_SCRIPT_UNSUPPORTED",
])

export function isDnsmasqSetupError(errorCode?: string | null): boolean {
  return errorCode != null && SETUP_ERROR_CODES.has(errorCode)
}
