import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"

export function WarningBanner() {
  return (
    <Alert className="mb-6 border-warning/50 bg-warning/5 text-warning-foreground">
      <AlertTitle>Pending restart</AlertTitle>
      <AlertDescription>
        Configuration has changed. Restart keen-pbr service to apply changes.
      </AlertDescription>
    </Alert>
  )
}
