import { toast } from "sonner"

import type { DnsCheckResponse } from "@/api/generated/model/dnsCheckResponse"
import i18n from "@/i18n"
import { DnsCheckResultDetails } from "@/pages/dns-servers-check"
import { summarizeCheck } from "@/pages/dns-servers-check-utils"

/**
 * Shows the result of a check as a notification. Problems stay on screen
 * until closed or replaced by the next check.
 */
export function showDnsCheckToast(response: DnsCheckResponse, draft: boolean) {
  const summary = summarizeCheck(response)
  const notify =
    summary.level === "error"
      ? toast.error
      : summary.level === "warning"
        ? toast.warning
        : toast.success
  const hasDetails = summary.level !== "ok" || draft
  notify(i18n.t(`pages.dnsServers.check.result.${summary.level}`), {
    id: DNS_CHECK_TOAST_ID,
    closeButton: true,
    richColors: true,
    duration: summary.level === "ok" ? undefined : Infinity,
    description: hasDetails ? (
      <DnsCheckResultDetails draft={draft} summary={summary} />
    ) : undefined,
  })
}

/** Shows a check that could not be run as a notification. */
export function showDnsCheckFailedToast(message: string) {
  toast.error(i18n.t("pages.dnsServers.check.result.failed", { message }), {
    id: DNS_CHECK_TOAST_ID,
    closeButton: true,
    richColors: true,
  })
}

const DNS_CHECK_TOAST_ID = "dns-check"
