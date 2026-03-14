import { Button } from "@/components/ui/button"

export function ActionButtons({ labels }: { labels: string[] }) {
  return (
    <div className="flex flex-wrap gap-1">
      {labels.map((label) => (
        <Button key={label} size="sm" variant="outline">
          {label}
        </Button>
      ))}
    </div>
  )
}
