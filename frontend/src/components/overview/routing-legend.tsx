import { CircleCheckBig, CircleOff, Route, RouteOff } from "lucide-react"
import { useTranslation } from "react-i18next"

export function RoutingLegend() {
  const { t } = useTranslation()

  return (
    <div className="space-y-2 text-sm">
      <div className="font-medium">{t("overview.routingLegend.title")}</div>
      <ul className="space-y-1">
        <li className="flex items-center gap-2">
          <CircleCheckBig className="h-4 w-4 text-green-600" />
          {t("overview.routingLegend.inLists")}
        </li>
        <li className="flex items-center gap-2">
          <CircleOff className="h-4 w-4 text-gray-400" />
          {t("overview.routingLegend.notInLists")}
        </li>
        <li className="flex items-center gap-2">
          <Route className="h-4 w-4 text-green-600" />
          {t("overview.routingLegend.inIpsetAndLists")}
        </li>
        <li className="flex items-center gap-2">
          <RouteOff className="h-4 w-4 text-gray-400" />
          {t("overview.routingLegend.notInIpsetAndNotInLists")}
        </li>
        <li className="flex items-center gap-2">
          <Route className="h-4 w-4 text-yellow-500" />
          {t("overview.routingLegend.inIpsetButShouldNotBe")}
        </li>
        <li className="flex items-center gap-2">
          <RouteOff className="h-4 w-4 text-red-600" />
          {t("overview.routingLegend.notInIpsetButShouldBe")}
        </li>
      </ul>
    </div>
  )
}
