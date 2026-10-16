import { Check, Copy, Terminal } from "lucide-react"
import { useEffect, useRef, useState } from "react"
import { useTranslation } from "react-i18next"

import {
  InputGroup,
  InputGroupAddon,
  InputGroupButton,
  InputGroupInput,
  InputGroupText,
} from "@/components/ui/input-group"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"

/** Read-only shell command with a copy button; a click selects and copies it. */
export function CommandCopyField({
  command,
  ariaLabel,
}: {
  command: string
  ariaLabel: string
}) {
  const { t } = useTranslation()
  const [copyFeedback, setCopyFeedback] = useState<
    "idle" | "copied" | "failed"
  >("idle")
  const resetTimerRef = useRef<number | null>(null)

  useEffect(() => {
    if (copyFeedback !== "copied") {
      return
    }

    resetTimerRef.current = window.setTimeout(() => {
      setCopyFeedback("idle")
      resetTimerRef.current = null
    }, 1000)

    return () => {
      if (resetTimerRef.current !== null) {
        window.clearTimeout(resetTimerRef.current)
        resetTimerRef.current = null
      }
    }
  }, [copyFeedback])

  return (
    <InputGroup>
      <InputGroupAddon>
        <InputGroupText>
          <Terminal className="h-4 w-4" />
        </InputGroupText>
      </InputGroupAddon>
      <InputGroupInput
        className="cursor-pointer font-mono text-sm"
        onClick={(event) => {
          event.currentTarget.select()
          void copyCommand(command, setCopyFeedback)
        }}
        readOnly
        value={command}
      />
      <InputGroupAddon align="inline-end">
        <Tooltip>
          <TooltipTrigger render={<InputGroupButton size="icon-xs" />}>
            <InputGroupButton
              aria-label={ariaLabel}
              onClick={() => void copyCommand(command, setCopyFeedback)}
              size="icon-xs"
            >
              {copyFeedback === "copied" ? (
                <Check className="text-emerald-600" />
              ) : (
                <Copy />
              )}
            </InputGroupButton>
          </TooltipTrigger>
          <TooltipContent>
            {copyFeedback === "copied"
              ? t("common.copied")
              : copyFeedback === "failed"
                ? t("common.clipboardUnavailable")
                : t("common.copy")}
          </TooltipContent>
        </Tooltip>
      </InputGroupAddon>
    </InputGroup>
  )
}

async function copyCommand(
  command: string,
  setCopyFeedback: (value: "idle" | "copied" | "failed") => void
) {
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(command)
      setCopyFeedback("copied")
      return
    }
  } catch {
    // Fall back to `execCommand("copy")` on insecure origins where the Clipboard API is unavailable.
  }

  if (copyCommandWithExec(command)) {
    setCopyFeedback("copied")
  } else {
    setCopyFeedback("failed")
  }
}

function copyCommandWithExec(command: string) {
  const textarea = document.createElement("textarea")
  textarea.value = command
  textarea.setAttribute("readonly", "")
  textarea.style.position = "fixed"
  textarea.style.top = "0"
  textarea.style.left = "0"
  textarea.style.opacity = "0"

  document.body.appendChild(textarea)
  textarea.focus()
  textarea.select()

  try {
    return document.execCommand("copy")
  } catch {
    return false
  } finally {
    document.body.removeChild(textarea)
  }
}
