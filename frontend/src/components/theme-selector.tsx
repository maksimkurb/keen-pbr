import { useTheme } from "@/components/theme-provider"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

const THEME_OPTIONS = [
  { value: "system", label: "Use system setting" },
  { value: "light", label: "Light" },
  { value: "dark", label: "Dark" },
] as const

export function ThemeSelector() {
  const { theme, setTheme } = useTheme()

  return (
    <div className="space-y-2">
      <p className="px-1 text-xs font-medium text-sidebar-foreground/70">Theme</p>
      <Select
        defaultValue={theme}
        onValueChange={(value) => setTheme(value as "system" | "light" | "dark")}
        value={theme}
      >
        <SelectTrigger aria-label="Theme selector" className="bg-sidebar">
          <SelectValue placeholder="Use system setting" />
        </SelectTrigger>
        <SelectContent align="start">
          {THEME_OPTIONS.map((option) => (
            <SelectItem key={option.value} value={option.value}>
              {option.label}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    </div>
  )
}
