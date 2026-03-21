import { CircleCheckBig, CircleOff, Route, RouteOff } from "lucide-react"

export function RoutingLegend() {
  return (
    <div className="space-y-2 text-sm">
      <div className="font-medium">Legend</div>
      <ul className="space-y-1">
        <li className="flex items-center gap-2">
          <CircleCheckBig className="h-4 w-4 text-green-600" />
          In domain/IP lists
        </li>
        <li className="flex items-center gap-2">
          <CircleOff className="h-4 w-4 text-gray-400" />
          Not in domain/IP lists
        </li>
        <li className="flex items-center gap-2">
          <Route className="h-4 w-4 text-green-600" />In IPSet and in lists
        </li>
        <li className="flex items-center gap-2">
          <RouteOff className="h-4 w-4 text-gray-400" />Not in IPSet and not in lists
        </li>
        <li className="flex items-center gap-2">
          <Route className="h-4 w-4 text-yellow-500" />In IPSet but should not be
        </li>
        <li className="flex items-center gap-2">
          <RouteOff className="h-4 w-4 text-red-600" />Not in IPSet but should be
        </li>
      </ul>
    </div>
  )
}
