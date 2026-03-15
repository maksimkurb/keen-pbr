import { Route, Switch } from "wouter"

import { AppShell } from "@/components/layout/app-shell"
import { DnsRulesPage } from "@/pages/dns-rules-page"
import { DnsServersPage } from "@/pages/dns-servers-page"
import { DnsServerUpsertPage } from "@/pages/dns-servers-upsert-page"
import { GeneralConfigPage } from "@/pages/general-config-page"
import { ListUpsertPage } from "@/pages/list-upsert-page"
import { ListsPage } from "@/pages/lists-page"
import { OutboundUpsertPage } from "@/pages/outbound-upsert-page"
import { OutboundsPage } from "@/pages/outbounds-page"
import { OverviewPage } from "@/pages/overview-page"
import { RoutingRulesPage } from "@/pages/routing-rules-page"

function App() {
  return (
    <AppShell>
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
            <OutboundUpsertPage mode="edit" outboundId={params.outboundId} />
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
        <Route component={DnsRulesPage} path="/dns-rules" />
        <Route component={RoutingRulesPage} path="/routing-rules" />
        <Route component={OverviewPage} />
      </Switch>
    </AppShell>
  )
}

export default App
