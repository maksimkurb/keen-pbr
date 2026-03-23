"use client"

import { Toaster as Sonner, type ToasterProps } from "sonner"

import { useTheme } from "@/components/theme-provider"

const resolveTheme = (theme: ReturnType<typeof useTheme>["theme"]) =>
  theme === "system" ? undefined : theme

export function Toaster({ ...props }: ToasterProps) {
  const { theme } = useTheme()

  return <Sonner theme={resolveTheme(theme)} {...props} />
}
