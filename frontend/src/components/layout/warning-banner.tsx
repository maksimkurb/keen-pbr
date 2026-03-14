import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"

export function WarningBanner() {
  return (
    <Alert className="mb-5 border-amber-300 bg-amber-50 text-amber-800">
      <AlertTitle>Pending restart</AlertTitle>
      <AlertDescription>
        Configuration has changed. Restart keen-pbr service to apply changes.
      </AlertDescription>
    </Alert>
  )
}
