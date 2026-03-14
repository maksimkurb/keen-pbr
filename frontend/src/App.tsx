import { Route, Switch } from "wouter"

import { AppShell } from "@/components/layout/app-shell"
import { DnsRulesPage } from "@/pages/dns-rules-page"
import { DnsServersPage } from "@/pages/dns-servers-page"
import { GeneralConfigPage } from "@/pages/general-config-page"
import { ListsPage } from "@/pages/lists-page"
import { OutboundsPage } from "@/pages/outbounds-page"
import { OverviewPage } from "@/pages/overview-page"
import { RoutingRulesPage } from "@/pages/routing-rules-page"

function App() {
  return (
    <AppShell>
      <Switch>
        <Route component={OverviewPage} path="/" />
        <Route component={GeneralConfigPage} path="/general" />
        <Route component={ListsPage} path="/lists" />
        <Route component={OutboundsPage} path="/outbounds" />
        <Route component={DnsServersPage} path="/dns-servers" />
        <Route component={DnsRulesPage} path="/dns-rules" />
        <Route component={RoutingRulesPage} path="/routing-rules" />
        <Route component={OverviewPage} />
      </Switch>
    </AppShell>
  )
}

export default App
