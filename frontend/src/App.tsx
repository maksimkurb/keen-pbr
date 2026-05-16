import { Suspense, lazy } from "react"
import { Route, Switch } from "wouter"

import { AppShell } from "@/components/layout/app-shell"
import { TableSkeleton } from "@/components/shared/table-skeleton"

const OverviewPage = lazy(() =>
  import("@/pages/overview-page").then((m) => ({ default: m.OverviewPage })),
)
const GeneralConfigPage = lazy(() =>
  import("@/pages/general-config-page").then((m) => ({
    default: m.GeneralConfigPage,
  })),
)
const DnsRuleUpsertPage = lazy(() =>
  import("@/pages/dns-rule-upsert-page").then((m) => ({
    default: m.DnsRuleUpsertPage,
  })),
)
const DnsRulesPage = lazy(() =>
  import("@/pages/dns-rules-page").then((m) => ({ default: m.DnsRulesPage })),
)
const DnsServersPage = lazy(() =>
  import("@/pages/dns-servers-page").then((m) => ({
    default: m.DnsServersPage,
  })),
)
const DnsServerUpsertPage = lazy(() =>
  import("@/pages/dns-servers-upsert-page").then((m) => ({
    default: m.DnsServerUpsertPage,
  })),
)
const ListUpsertPage = lazy(() =>
  import("@/pages/list-upsert-page").then((m) => ({
    default: m.ListUpsertPage,
  })),
)
const ListsPage = lazy(() =>
  import("@/pages/lists-page").then((m) => ({ default: m.ListsPage })),
)
const OutboundUpsertPage = lazy(() =>
  import("@/pages/outbound-upsert-page").then((m) => ({
    default: m.OutboundUpsertPage,
  })),
)
const OutboundsPage = lazy(() =>
  import("@/pages/outbounds-page").then((m) => ({ default: m.OutboundsPage })),
)
const RoutingRuleUpsertPage = lazy(() =>
  import("@/pages/routing-rule-upsert-page").then((m) => ({
    default: m.RoutingRuleUpsertPage,
  })),
)
const RoutingRulesPage = lazy(() =>
  import("@/pages/routing-rules-page").then((m) => ({
    default: m.RoutingRulesPage,
  })),
)

function RouteSuspenseFallback() {
  return (
    <div className="space-y-4 px-4 py-6 md:px-6">
      <TableSkeleton />
    </div>
  )
}

function App() {
  return (
    <AppShell>
      <Suspense fallback={<RouteSuspenseFallback />}>
        <Switch>
          <Route component={OverviewPage} path="/" />
          <Route component={GeneralConfigPage} path="/general" />
          <Route path="/lists/create">
            <ListUpsertPage mode="create" />
          </Route>
          <Route path="/lists/:listId/edit">
            {(params) => <ListUpsertPage listId={params.listId} mode="edit" />}
          </Route>
          <Route component={ListsPage} path="/lists" />
          <Route path="/outbounds/create">
            <OutboundUpsertPage mode="create" />
          </Route>
          <Route path="/outbounds/:outboundId/edit">
            {(params) => (
              <OutboundUpsertPage
                mode="edit"
                outboundId={params.outboundId}
              />
            )}
          </Route>
          <Route component={OutboundsPage} path="/outbounds" />
          <Route path="/dns-servers/create">
            <DnsServerUpsertPage mode="create" />
          </Route>
          <Route path="/dns-servers/:serverTag/edit">
            {(params) => (
              <DnsServerUpsertPage
                mode="edit"
                serverTag={decodeURIComponent(params.serverTag)}
              />
            )}
          </Route>
          <Route component={DnsServersPage} path="/dns-servers" />
          <Route path="/dns-rules/create">
            <DnsRuleUpsertPage mode="create" />
          </Route>
          <Route path="/dns-rules/:ruleIndex/edit">
            {(params) => (
              <DnsRuleUpsertPage mode="edit" ruleIndex={params.ruleIndex} />
            )}
          </Route>
          <Route component={DnsRulesPage} path="/dns-rules" />
          <Route path="/routing-rules/create">
            <RoutingRuleUpsertPage mode="create" />
          </Route>
          <Route path="/routing-rules/:ruleIndex/edit">
            {(params) => (
              <RoutingRuleUpsertPage
                mode="edit"
                ruleIndex={params.ruleIndex}
              />
            )}
          </Route>
          <Route component={RoutingRulesPage} path="/routing-rules" />
          <Route component={OverviewPage} />
        </Switch>
      </Suspense>
    </AppShell>
  )
}

export default App
