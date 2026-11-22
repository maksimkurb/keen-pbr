/**
 * Copies text to the clipboard. The router UI is usually served over plain
 * HTTP, where `navigator.clipboard` is unavailable, so fall back to
 * `document.execCommand("copy")`.
 */
export async function copyToClipboard(text: string): Promise<boolean> {
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text)
      return true
    }
  } catch {
    // Fall back to the legacy copy API on insecure origins.
  }
  return copyWithExecCommand(text)
}

function copyWithExecCommand(text: string): boolean {
  const textarea = document.createElement("textarea")
  textarea.value = text
  textarea.setAttribute("readonly", "")
  textarea.style.position = "fixed"
  textarea.style.top = "0"
  textarea.style.left = "0"
  textarea.style.opacity = "0"

  // Inside a modal dialog the focus trap would steal focus from a textarea
  // appended to <body>, so attach it to the open dialog when there is one.
  const host =
    document.activeElement?.closest<HTMLElement>('[role="dialog"]') ??
    document.body
  host.appendChild(textarea)
  textarea.focus()
  textarea.select()

  try {
    return document.execCommand("copy")
  } catch {
    return false
  } finally {
    host.removeChild(textarea)
  }
}
