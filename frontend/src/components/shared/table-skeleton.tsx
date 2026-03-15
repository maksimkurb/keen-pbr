import { Skeleton } from "@/components/ui/skeleton"

export function TableSkeleton({ rows = 3 }: { rows?: number }) {
  return (
    <div className="space-y-2">
      {Array.from({ length: rows }, (_, index) => (
        <Skeleton className="h-10 w-full" key={index} />
      ))}
    </div>
  )
}
