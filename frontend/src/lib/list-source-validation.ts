export function validateListSource(
  value: {
    source: string
    url: string
    file: string
    domains: string
    ip_cidrs: string
  },
  t: (key: string) => string
): Record<string, string> {
  if (value.source === "url" && !value.url.trim()) {
    return { url: t("common.validation.required") }
  }
  if (value.source === "file" && !value.file.trim()) {
    return { file: t("common.validation.required") }
  }
  if (
    value.source === "inline" &&
    !value.domains.trim() &&
    !value.ip_cidrs.trim()
  ) {
    const message = t("pages.listUpsert.validation.inlineRequired")
    return { domains: message, ip_cidrs: message }
  }
  return {}
}
