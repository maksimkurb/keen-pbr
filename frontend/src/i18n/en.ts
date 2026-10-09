export const enTranslation = {
  auth: {
    loading: "Loading keen-pbr…",
    title: "Sign in to keen-pbr",
    description: "Enter the administrator password to continue.",
    password: "Password",
    signIn: "Sign in",
    signingIn: "Signing in…",
    invalid: "Invalid password",
    failed: "Unable to sign in",
    signOut: "Sign out",
    warning: {
      prefix: "Authentication is disabled. ",
      action: "Set an administrator password",
      suffix: " to protect the API and WebUI.",
    },
    settings: {
      title: "Authentication and CORS",
      description:
        "Protect the WebUI and API with the administrator password. A new UI login signs out the previous UI session.",
      enable: "Enable authentication",
      newPassword: "New password",
      newPasswordPlaceholder: "Enter a new password",
      passwordSetPlaceholder: "Password is set — enter a new one to replace it",
      confirmPassword: "Confirm password",
      allowedOrigins: "Allowed CORS origins",
      originsPlaceholder:
        "https://panel.example.com\nhttps://admin.example.net",
      originsDescription:
        "One exact HTTP or HTTPS origin per line. Only configure if you use external UIs to manage the keen-pbr API.",
      passwordMismatch: "Passwords do not match.",
      passwordRequired: "Set a password before enabling authentication.",
      updateFailed: "Unable to update authentication settings.",
      invalidOrigin:
        "Each CORS entry must be an exact HTTP or HTTPS origin without a path, credentials, query, fragment, or wildcard.",
      saved: "Authentication settings updated.",
    },
  },
  common: {
    add: "Add",
    documentation: "Documentation",
    documentationUrl: "https://keen-pbr.fyi/docs/",
    language: "Language",
    theme: "Theme",
    enabled: "Enabled",
    disabled: "Disabled",
    close: "Close",
    cancel: "Cancel",
    copy: "Copy",
    copied: "Copied",
    clipboardUnavailable: "Clipboard unavailable",
    edit: "Edit",
    delete: "Delete",
    moveUp: "Move up",
    moveDown: "Move down",
    dragToReorder: "Drag to reorder",
    unableToLoadData: "Unable to load data",
    loadErrorDescription:
      "We can't load data right now. Try refreshing the page.",
    noneShort: "-",
    multiSelectList: {
      addItem: "Add item",
      emptyMessage: "No items found.",
      availableItems: "Available items",
      noItemsSelected: "No items selected",
      addFirstItem: "Add your first item to start building this list.",
      removeItem: "Remove {{item}}",
    },
    listUsage: {
      usedElsewhere: "Also in: {{summary}}",
    },
    interfacePicker: {
      open: "Open interface picker",
      empty: "No interfaces found.",
      notExists: "(not exists)",
      notFound: "Interface does not exist.",
    },
    validation: {
      required: "This field is required",
      tagNamePattern:
        "Can only contain a-z, 0-9 and underscores. Max 24 characters, must start with a letter.",
    },
    ruleNumber: "Rule #{{number}}",
    rowActions: "Actions",
    selection: {
      selectedOfTotal: "Selected {{count}}/{{total}}",
      selected: "Selected: {{count}}",
      select: "Select multiple",
      selectAll: "Select all",
      selectRow: "Select {{rowLabel}}",
    },
  },
  runtime: {
    healthy: "Healthy",
    notHealthy: "Not healthy",
    activeOutbound: "Active outbound {{value}}",
    activeInterface: "Active {{value}}",
    outboundStatus: {
      healthy: "Healthy",
      degraded: "Degraded",
      unavailable: "Unavailable",
      unknown: "Unknown",
    },
    interfaceStatus: {
      active: "Active",
      backup: "Backup",
      degraded: "Degraded",
      unavailable: "Unavailable",
      unknown: "Unknown",
    },
    statusTone: {
      healthy: "Healthy",
      degraded: "Degraded",
      unknown: "Unknown",
    },
    fallback: {
      table: "Routing table {{value}}",
      blackhole: "Block all incoming traffic",
    },
  },
  language: {
    selectorAria: "Language selector",
    english: "English",
    russian: "Russian",
  },
  theme: {
    selectorAria: "Theme selector",
    useSystem: "Use system setting",
    light: "Light",
    dark: "Dark",
  },
  nav: {
    groups: {
      general: "General",
      routing: "Routing",
      dns: "DNS",
    },
    items: {
      systemMonitor: "Dashboard",
      requestsLog: "Requests log",
      settings: "Settings",
      security: "Security",
      outbounds: "Outbounds",
      dnsServers: "DNS servers",
      lists: "Lists",
      routingRules: "Routing rules",
      dnsRules: "DNS Rules",
    },
  },
  brand: {
    logoAlt: "keen-pbr logo",
    tagline: "Get packets sorted",
    openMenu: "Open menu",
  },
  warning: {
    draftChanged: "Configuration was changed. Save it to disk to apply it.",
    actions: {
      applying: "Applying...",
      apply: "Apply",
      discarding: "Discarding...",
      discard: "Discard",
      rollingBack: "Rolling back...",
      rollback: "Roll back",
      applyingAndRestarting: "Applying & Restarting...",
      applyAndRestart: "Apply & Restart",
      restarting: "Restarting...",
      restart: "Restart",
    },
    compact: {
      keenRestartRequired: "Pending changes",
      keenRestartRequiredDescription:
        "New settings found. Apply to restart keen-pbr.",
      runtimeReloading: "Reloading keen-pbr...",
      runtimeReloadingDescription: "Current stage: {{stage}}",
      runtimeReloadSucceeded: "keen-pbr is ready",
      runtimeReloadSucceededDescription:
        "Routing is serving the expected configuration.",
      runtimeApplying: "Applying keen-pbr configuration...",
      runtimeApplySucceeded: "keen-pbr configuration applied",
      runtimeApplyFailed: "keen-pbr configuration could not be applied",
      runtimeRollingBack: "Rolling back keen-pbr configuration...",
      runtimeRollbackSucceeded: "Previous configuration restored",
      runtimeRollbackFailed: "Configuration rollback failed",
      runtimeStarting: "Starting keen-pbr...",
      runtimeStartSucceeded: "keen-pbr is running",
      runtimeStartFailed: "keen-pbr could not be started",
      runtimeStopping: "Stopping keen-pbr...",
      runtimeStopSucceeded: "keen-pbr is stopped",
      runtimeStopFailed: "keen-pbr could not be stopped",
      runtimeStartingDescription:
        "Routing and firewall are starting. Please wait.",
      runtimeReloadFailed: "keen-pbr reload failed",
      runtimeReloadFailedDescription:
        "The routing runtime could not finish reloading. Try Apply & Restart again.",
    },
    full: {
      unsavedTitle: "Configuration is unsaved",
    },
  },
  lifecycle: {
    stages: {
      validate_config: "Validate configuration",
      prepare_remote_lists: "Prepare remote lists",
      reconcile_runtime: "Reconcile routing and firewall",
      stop_routing: "Stop routing and firewall",
      start_routing: "Start routing and firewall",
      commit_config: "Commit configuration",
      restore_config: "Restore previous configuration",
    },
  },
  overview: {
    status: {
      ok: "Everything works",
      degraded: "Routing works with limitations",
      stopped: "Service is stopped",
      issuesPrefix: "System is running, but there are",
      issueCount_one: "{{count}} problem",
      issueCount_other: "{{count}} problems",
      versionLine: "keen-pbr {{version}} · build {{build}} · {{os}}",
      confirmStop: "Stop the service?",
      confirmStopAction: "Yes, stop",
      busy: {
        start: "Service is starting…",
        stop: "Service is stopping…",
        restart: "Service is restarting…",
        apply: "Applying configuration…",
      },
      busyAction: {
        start: "Starting…",
        stop: "Stopping…",
        restart: "Restarting…",
        apply: "Applying…",
      },
    },
    diagnostics: {
      showHealthy: "Show healthy",
      noIssues: "No problems",
      open: "Open",
    },
    issues: {
      capabilityUnsupported: "Not supported by the kernel",
      capabilityFallback:
        "Not available in the kernel; plain address rules are used instead",
      kernelCheckFailed: "Kernel check failed: {{reason}}",
      interceptLimited: "Traffic interception is limited",
      interceptWarning: "Traffic interception warning",
      routingCheckFailed: "Routing checks failed",
      chainMissing: "Firewall chain is missing",
      preroutingMissing: "Firewall chain is not hooked into PREROUTING",
      firewallRules: "Firewall rules do not match: {{count}}",
      routes: "Routing tables do not match: {{count}}",
      policies: "Policy rules do not match: {{count}}",
      dnsmasqError: "dnsmasq does not serve the DNS rules",
      dnsmasqDead: "dnsmasq is not running",
      dnsmasqReconciling: "DNS rules are being repaired",
      outboundUnavailable: "{{tag}} is unavailable",
      outboundDegraded: "{{tag}} is degraded",
      groupMembersFailing: "{{tag}}: failing members: {{count}}",
    },
    healthy: {
      firewallOk: "chain and PREROUTING hook in place",
      firewallPartial: "chain is incomplete",
      kernelWithRelease: "Kernel checks ({{release}})",
      passed: "{{passed}} of {{total}} passed",
    },
    counters: {
      title: "Counters",
      collapse: "Collapse",
      showAll: "All {{count}}",
      short: {
        dnsPackets: "DNS",
        dnsMatched: "DNS matched",
        l7Packets: "L7",
        l7Matched: "L7 matched",
      },
      groups: {
        dns: "DNS",
        l7: "L7 and marker",
        sets: "Sets",
        conntrack: "Conntrack",
        queue: "Queue",
      },
    },
    pageDescription:
      "Overview of routing runtime, config state, and active outbounds",
    runtime: {
      title: "Routing runtime",
      loadError: "Failed to load routing runtime state.",
      version: "Version",
      build: "Build",
      router: "Router",
      routingStatus: "Routing status",
      actions: {
        start: "Start",
        stop: "Stop",
        restart: "Restart",
      },
    },
    outbounds: {
      title: "Outbounds",
      summary: "{{configured}} configured · {{groups}} groups",
      online: "{{count}} online",
      manage: "Manage",
      plainTitle: "Plain outbounds",
      probePassed: "Network available",
      interfaceUp: "Interface is up; connectivity is not checked",
      plainActive: "Active; connectivity is not checked",
      latency: "{{value}} ms",
      packetsTitle: "{{received}} of {{attempted}} probe replies",
      columns: {
        group: "Group",
        strategy: "Strategy",
        members: "Members",
      },
      loadError: "Unable to load outbound health.",
      emptyTitle: "No outbounds configured",
      emptyDescription: "Add outbounds to see health checks.",
      inUse: "In use",
      urltestTitle: "urltest",
      headers: {
        tag: "Tag",
        destination: "Destination",
        status: "Status",
      },
      destination: {
        interface: "Interface {{name}}",
        interfaceWithGateway: "Interface {{name}} (gw: {{gateway}})",
        table: "Table {{value}}",
        outbound: "Outbound {{name}}",
      },
    },
    routing: {
      title: "Diagnostics",
      loadError: "Unable to load routing checks.",
      emptyTitle: "No routing checks reported yet",
      emptyDescription:
        "Routing checks will appear after the next apply or runtime restart.",
      showHealthyEntries: "Show healthy entries too",
      allHealthyTitle: "Everything is good",
      allHealthyDescription: "No failing routing health entries right now.",
      noChecksTitle: "No checks reported",
      noChecksDescription: "Routing health has no entries to display.",
      sections: {
        firewall: "Firewall",
        routes: "Routes",
        policies: "Policies",
      },
      chain: "chain",
      prerouting: "prerouting",
      kernel: "kernel",
      defaultRoute: "default",
      ipv4: "IPv4",
      ipv6: "IPv6",
      yes: "yes",
      no: "no",
      tableLabel: "table {{value}}",
      priorityLabel: "priority {{value}}",
      fwmarkLabel: "fwmark {{value}}",
      fwmarkExpectedActual: "expected {{expected}}, got {{actual}}",
      actualLabel: "actual {{value}}",
      routeTypeFallback: "route",
      routeVia: "via {{value}}",
      routeGateway: "gw {{value}}",
      routeMetric: "metric {{value}}",
      issues: {
        tableMissing: "table missing",
        defaultRouteMissing: "default route missing",
        interfaceMismatch: "interface mismatch",
        gatewayMismatch: "gateway mismatch",
      },
    },
    diagnosticsDownload: {
      button: "Download report",
      modal: {
        title: "Warning: sensitive data",
        description: "The diagnostics file includes:",
        items: {
          config: "Your full configuration file (including the lists in use)",
          serviceHealth: "Service health",
          routingHealth: "Routing health",
          outbounds: "Outbounds status",
          names: "Names of lists, outbounds, and interfaces",
        },
        trustWarning: "Please share this file only with people you trust.",
        hideListsOption: "Hide list contents and list URLs",
        downloadAction: "Download report",
      },
    },
    dnsCheck: {
      card: {
        title: "DNS interception",
        description:
          "Observes DNS traffic through keen-pbr from this browser or another device; it does not verify synthetic response replacement.",
        disabledDescription:
          "Enable DNS interception in the settings to run the DNS self-check.",
        runtimeDisabledDescription:
          "The DNS interception runtime is unavailable, so the marker self-check is disabled.",
        configuredServers: "Configured DNS servers",
        noServers:
          "No upstream DNS servers are configured on the DNS Servers page.",
        via: "via {{detour}}",
        checking: "Checking...",
        checkAgain: "Check again",
        testFromPc: "Test from another device",
      },
      modal: {
        title: "Test DNS from another device",
        description:
          "Run the generated `nslookup` command on your PC or phone while this dialog stays open.",
        copyCommand: "Copy and run this command:",
        warning:
          "The DNS test query has not arrived yet. Make sure the device is using your router DNS and try the command again.",
        copyAria: "Copy command",
      },
      status: {
        disabled: "DNS interception is disabled in config.",
        runtimeDisabled:
          "Interception is not working: the DNS hold is not active",
        browserSuccess: "DNS interception from this browser is working",
        manualProbeSuccess:
          "DNS request from the device was observed by the interceptor.",
        browserProbeFail:
          "Browser request completed, but the interceptor did not see the marker lookup.",
        sseUnavailable:
          "The live DNS event stream is unavailable, so the check could not start.",
        browserFail:
          "Browser request ran, but the interceptor did not observe the marker lookup.",
        sseFail: "Live DNS event stream is not connected.",
        sseStalled:
          "The browser could not open the live DNS event stream. Too many keen-pbr tabs may be open (browsers allow 6 connections per site) — close other keen-pbr tabs and retry.",
        sseHttp: "The live DNS event stream request failed (HTTP {{status}}).",
        browserChecking: "Checking browser DNS path...",
        browserUnknown: "Browser DNS status is not known yet.",
        manualSuccess:
          "DNS request from the device was observed by the interceptor.",
        manualWaiting: "Waiting for your manual nslookup command...",
        manualIncomplete: "Manual device test has not completed yet.",
      },
    },
    dnsRules: {
      server: "DNS server",
      inactive: "Not active",
      rulesAndDomains: "Rules / domains",
      title: "DNS Rules",
      state: {
        ok: "In sync",
        applying: "Applying",
        reconciling: "Reconciling",
        error: "Error",
        disabled: "Disabled",
      },
      rules: "DNS rules",
      domains: "Domains",
      lastSync: "Last sync",
      loadedAt: "dnsmasq loaded the config at",
      externalReload: "dnsmasq restarted outside keen-pbr at",
      disabledDescription: "DNS rules integration is disabled.",
      alive: {
        label: "dnsmasq service",
        dead: "Dead",
        unknown: "Unknown",
      },
      repairScheduled: "Repair attempt {{n}}/{{max}} at {{time}}.",
      repairRestarting: "Restarting dnsmasq (attempt {{n}}/{{max}}).",
      repairPaused: "Automatic repair paused after {{max}} attempts.",
      repairPausedHint: "Apply or Restart re-arms automatic repair.",
      lastError: "Last error",
    },
    intercept: {
      title: "Domain-based routing",
      rows: {
        dns: {
          title: "DNS interception",
          description: "learns domains from DNS answers",
        },
        dnsHold: {
          title: "DNS hold",
          description: "answer waits until sets are filled",
        },
        l7: {
          title: "L7 interception",
          description: "TLS SNI · HTTP Host · QUIC",
        },
      },
      description: "Daemon-side DNS and L7 interception health.",
      status: {
        disabled: "Disabled",
        running: "Running",
        stopped: "Not running",
      },
      dnsHoldActive: "DNS hold active",
      dnsHoldInactive: "DNS hold inactive",
      l7Active: "L7 active",
      l7Inactive: "L7 inactive",
      summary: {
        processor: "DNS/DPI processor",
        dnsHold: "DNS hold",
        dpi: "DPI (SNI/Host)",
        enabled: "Enabled",
        disabled: "Disabled",
      },
      checksTitle: "Kernel checks",
      countersTitle: "Interception counters",
      moreCounters: "Show detailed counters",
      supported: "supported",
      unsupported: "unsupported",
      unsupportedWarning: "Some interception features are unavailable",
      diagnosticErrors: "Kernel checks reported errors",
      capabilities: {
        nfqueue: "NFQUEUE",
        nflog: "NFLOG",
        connbytes: "connbytes",
        addrtype: "addrtype",
      },
      probes: {
        title: "Kernel probes",
        kernel: "Kernel {{release}}",
        status: {
          ok: "ok",
          unsupported: "unsupported",
          error: "error",
          skipped: "skipped",
          not_run: "not run",
        },
      },
      kernelQueue: {
        queueTotal: "Queued",
        queueDropped: "Kernel dropped",
        userDropped: "User dropped",
        idSequence: "Kernel sequence",
      },
      counters: {
        dnsPackets: "DNS packets",
        dnsParseErrors: "DNS parse errors",
        dnsMatched: "DNS matched",
        dnsHoldTimeouts: "DNS hold timeouts",
        dnsTcpPartial: "DNS TCP partial",
        markerHits: "Marker hits",
        l7Packets: "L7 packets",
        l7Matched: "L7 matched",
        setAdded: "Set entries added",
        setRefreshed: "Set entries refreshed",
        setErrors: "Set errors",
        conntrackRequests: "Conntrack requests",
        conntrackDeleted: "Conntrack deleted",
        conntrackErrors: "Conntrack errors",
        queueOverruns: "Queue overruns",
        logOverruns: "Log overruns",
      },
    },
    routingTest: {
      protocol: "Protocol",
      port: "Destination port",
      otherProtocol: "Other",
      otherCriteria: "Other criteria",
      sourceIpPlaceholder: "Device IP, e.g. 192.168.1.10",

      title: "Where does this traffic go?",
      description: "Check a domain or IP address",
      placeholder: "e.g. google.com or 1.2.3.4",
      submit: "Check route",
      invalidTarget: "Please enter a valid domain or IP.",
      requestFailed: "Routing test failed. Please try again.",
      emptyTitle: "No route matched",
      emptyDescription: "Try another domain or IP address.",
    },
    routingDiagnostics: {
      trace: {
        writeEvidence: {
          title: "Dynamic write history",
          recorded:
            "keen-pbr successfully added, refreshed or confirmed this IP in the set.",
          age: "Latest retained operation: {{age}} s ago.",
          nowMissing:
            "A successful operation was recorded, but the IP is absent now. This cache does not record why it later disappeared.",
          no_record:
            "No successful operation for this IP and set is retained in the cache.",
          not_tracked:
            "Dynamic write history for this set is currently unavailable.",
          unavailable: "Write history was not provided.",
          scope:
            "The cache stores IPs and sets, not domains or DNS queries. It can be reset or evict records: missing history does not mean the IP was never added.",
        },

        allRoutesMatch: "The routes match the rules",
        routeProblems: "Route problems detected",
        ipResults: "Matching routes: {{count}} of {{total}}",
        matches: "Matches",
        differs: "Mismatch",
        pathFor: "Active path for {{outbound}}",

        outboundUnavailable:
          "The service reports outbound {{outbound}} as unavailable. A matching route does not confirm that it can carry traffic.",
        dns: "DNS resolution",
        dnsEmpty: "DNS returned no IP addresses for this domain.",
        literalIp: "IP {{ip}} was supplied directly; no DNS query is needed.",
        list: "List matching",
        noListNeeded: "This rule uses other criteria, without a list.",
        rule: "Rule selection",
        selectedRule: "Rule #{{rule}} → {{outbound}}",
        systemRule: "No rule matched. The system route is used.",
        unknownRule:
          "There is not enough information to select a rule reliably.",
        firewall: "Firewall check",
        step: "Step {{step}} of {{total}}",
        configuredPath: "Current path of the expected outbound",
        expectedSetMissing:
          "The IP is also absent from the expected rule’s IPSet.",
        notConnectivityTest:
          "This checks the route from firewall state, not website connectivity.",
        dnsAdvice:
          "The device’s DNS query may not have passed through the router yet. Open the site on a device using the router’s DNS, then check again.",
        issues: {
          ok: {
            title: "The route matches the rule",
            reason: "The expected outbound matches the firewall route.",
            advice: "",
          },
          dns: {
            title: "No IP address resolved",
            reason:
              "Without an IP address, firewall membership cannot be checked.",
            advice: "Check the domain and the router’s DNS, then try again.",
          },
          criteria: {
            title: "The route cannot be determined yet",
            reason:
              "Packet criteria are incomplete, address families are incompatible, or default-gateway directness cannot be evaluated.",
            advice:
              "Provide source IP, source port and DSCP if required by the rules. This check cannot establish whether an address is directly connected for default-gateway rules.",
          },
          firewall: {
            title: "The actual route is unknown",
            reason:
              "Live set membership or applied rule criteria could not be checked reliably.",
            advice:
              "Check service status and detailed diagnostics. An unavailable check does not mean the IP is absent from an IPSet.",
          },
          missing_ipset: {
            title: "IP missing from the expected IPSet",
            reason:
              "The address matches the list but is absent from the selected rule’s IPSet.",
            advice:
              "Check applied configuration and list updates, then try again.",
          },
          other_ipset: {
            title: "IP found in another IPSet",
            reason:
              "The address is absent from the expected set but present in another rule’s set for the actual outbound.",
            advice:
              "Check rule order and set membership in detailed diagnostics. Set membership alone does not prove which rule won.",
          },
          conflicting_ipsets: {
            title: "IP present in multiple IPSets",
            reason:
              "The address is in the expected set and another rule’s set, but the firewall chooses a different outbound.",
            advice:
              "An earlier rule may have priority. Check rule order and current set contents.",
          },
          stale_ipset: {
            title: "IPSet differs from list contents",
            reason:
              "The address is in a set for the actual outbound but does not match that rule’s current lists.",
            advice:
              "Check list updates and applied configuration. The set entry may be stale.",
          },
          mismatch: {
            title: "The route differs from the expected route",
            reason:
              "The expected and firewall outbounds differ. There is not enough evidence to identify the exact cause.",
            advice:
              "Check applied configuration, rule order and detailed diagnostics.",
          },
        },
      },

      expectedRouteUnknown:
        "Not enough information to predict the route for {{target}}",
      ruleDetailsTitle: "Diagnostics",

      defaultRoute: "System route",
      unknownRoute: "Unknown",
      chainTarget: "Target",
      chainList: "List",
      chainRule: "Rule",
      noListMatch: "No match",
      activePaths: "Active path",
      expectedRouteTitle: "{{target}} should use {{outbound}}",
      actualRouteTitle: "Actual route: {{outbound}}",
      ruleConditionsLabel: "Rule conditions",
      routeConfirmed: "The firewall route matches the expected route.",
      routeUnavailable:
        "The actual route could not be determined from firewall state and the supplied criteria.",
      dnsPendingHint:
        "This IP is not yet in the rule’s IPSet. The device’s DNS request may not have reached the router. Open the site on a device using the router’s DNS, then check again.",
      routeMismatchHint:
        "Firewall state differs from the expected route. See rule diagnostics for details.",
      recheck: "Check again",

      noMatchingRule: "No routing rule matches the supplied parameters.",
      resultTitle: "Routing result",
      ip: "IP",
      resultListMatch: "List Match",
      resultListMatchVia: "{{list}} (entry <code>{{via}}</code>)",
      expectedOutbound: "Expected Outbound",
      actualOutbound: "Actual Outbound",
      status: "Status",
      hostLabel: 'Host "{{target}}"',
      inRuleLists: "In rule domain/IP lists?",
      showAllRules: "Show all rules",
      listMatch: "{{list}}: {{via}}",
      noConditions: "No extra conditions",
      conditions: {
        dscp: "DSCP",
        lists: "Lists",
        proto: "Protocol",
        sourceIp: "Source IP",
        destinationIp: "Destination IP",
        sourcePort: "Source port",
        destinationPort: "Destination port",
      },
    },
    routingLegend: {
      title: "Legend",
      inLists: "In domain/IP lists",
      notInLists: "Not in domain/IP lists",
      inIpsetAndLists: "In IPSet and in lists",
      notInIpsetAndNotInLists: "Not in IPSet and not in lists",
      inIpsetButShouldNotBe: "In IPSet but should not be",
      notInIpsetButShouldBe: "Not in IPSet but should be",
    },
  },
  requestsLog: {
    empty: "No requests observed yet.",
    gap: "Events {{from}}–{{to}} were lost before delivery.",
    copyIps: "Copy IP addresses: {{value}}",
    filters: {
      count: "{{shown}} of {{total}}",
      clear: "Clear filters",
      noMatches: "No requests match the filters.",
      invalidIp: "Invalid CIDR subnet; the IP filter is ignored.",
      hideEmptyAnswers: "Hide empty answers",
      placeholder: {
        device: "192.168.1.*",
        domain: "*.example.com",
        ip: "10.0.0.0/8, 192.168.*",
      },
      hint: {
        device:
          "Wildcards: * any text, ? one character. Without wildcards, matches a substring.",
        domain:
          "Wildcards: * any text, ? one character. Without wildcards, matches a substring.",
        ip: "Exact IP, wildcard (10.0.*, *::1) or CIDR subnet (10.0.0.0/8, 2001:db8::/32). Matches any resolved address.",
      },
    },
    columns: {
      device: "Device",
      method: "Method",
      domain: "Domain",
      lists: "Lists",
      ip: "IP",
      processingTime: "Processing time",
      flags: "Flags",
    },
    timeout: {
      budget_spent_by_batch: {
        label: "late: batch",
        tooltip:
          "Earlier packets in the same wakeup used up the shared hold budget, so the answer was released before this one's set write.",
      },
      admission_blocked: {
        label: "late: apply",
        tooltip:
          "The hold deadline passed while the set write waited for a firewall apply to finish.",
      },
      own_write_slow: {
        label: "late: write",
        tooltip: "The set write itself took longer than the hold deadline.",
      },
      late_batch_full: {
        label: "dropped: batch full",
        tooltip:
          "The pending late-write queue was full, so these set elements were dropped.",
      },
      other: {
        label: "late: other",
        tooltip: "The hold deadline passed for another reason.",
      },
    },
    methods: {
      dns: "DNS",
      http: "HTTP Host",
      sni: "HTTPS SNI",
      quic: "QUIC",
      marker: "DNS marker",
    },
    methodTooltips: {
      dns: "DNS response",
      http: "HTTP request (Host header)",
      sni: "HTTPS connection (TLS SNI)",
      quic: "QUIC connection (Initial SNI)",
      marker: "DNS check marker",
    },
    decimalSeparator: ".",
    units: { us: "µs", ms: "ms", s: "s" },
    dnsReasons: {
      nxdomain: "NXDOMAIN",
      servfail: "SERVFAIL",
      refused: "REFUSED",
      rcode: "RCODE {{code}}",
      nodata: "no {{type}}",
      nodataOther: "{{type}} record",
    },
    dnsTooltips: {
      nxdomain: "Domain does not exist (NXDOMAIN)",
      servfail: "Server failure (SERVFAIL)",
      refused: "Query refused (REFUSED)",
      rcode: "DNS error code {{code}}",
      nodata: "The resolver answered NOERROR without {{type}} records (NODATA)",
      nodataOther:
        "The resolver answered NOERROR without {{type}} records (NODATA)",
    },
    flags: {
      added_one: "{{count}} new address added to the routing set",
      added_other: "{{count}} new addresses added to the routing set",
      refreshed_one:
        "{{count}} address was already in the set; its timeout was refreshed",
      refreshed_other:
        "{{count}} addresses were already in the set; their timeout was refreshed",
      errors_one: "{{count}} address failed to be written to the set",
      errors_other: "{{count}} addresses failed to be written to the set",
      not_learned_one: "{{count}} blocking or unroutable address not learned",
      not_learned_other:
        "{{count}} blocking or unroutable addresses not learned",
      not_learned_tooltip:
        "Blocking or unroutable answer (0.0.0.0, ::, loopback) — not added to sets",
      seq: "Request #{{seq}} (event sequence number)",
      time: "Seen at {{time}} ({{date}})",
      parse: "Response parsed in {{value}}",
      setWrite: "Set write took {{value}}",
    },
  },
  pages: {
    security: {
      title: "Security",
      description: "Manage access to the Web UI and API.",
    },
    settings: {
      title: "Settings",
      description:
        "Global defaults that apply to all your outbounds and rules.",
      saved: "Settings staged. Apply new config to persist them.",
      webUi: {
        settingsModeLabel: "Settings mode",
        simpleMode: "Simple",
        advancedMode: "Advanced",
        settingsModeHint:
          "Advanced mode exposes low-level routing parameters such as the fwmark mask and netlink queue and log group numbers. Do not change these parameters unless you fully understand them, as this may disrupt the correct operation of your device.",
        title: "Web UI",
        description: "Customize how this keen-pbr installation is identified.",
      },
      dns: {
        disabledTitle: "Local DNS server management is disabled",
        disabledDescription:
          "Enable dnsmasq management in settings to unlock DNS server and rule configuration",
        disabledAction: "Go to setting",
        title: "Local DNS server management",
        description:
          "Configure an upstream DNS server through keen-pbr and use separate DNS servers to resolve specific domains.",
        resolverIntegrationLabel: "Manage dnsmasq configuration",
        resolverIntegrationHint:
          "Example: Use DNS 8.8.8.8 by default, but resolve *.corp.acme domains using DNS 10.10.10.10.",
      },
      general: {
        title: "General",
        description: "Default behavior for all outbounds.",
        deviceNameLabel: "Device name",
        deviceNamePlaceholder: "For example, Home router",
        deviceNameHint:
          "Shown in the browser page title and under the keen-pbr logo. Leave empty to use the default branding.",
        strictEnforcementLabel:
          "Block traffic when an outbound is unavailable (kill-switch)",
        strictEnforcementHint:
          "If a VPN or interface goes offline, traffic matching its rules is blocked instead of falling back to the main routing table. Can be overridden per outbound.",
        skipMarkedPacketsLabel: "Skip packets that are already marked",
        skipMarkedPacketsHint:
          "Ignore packets with a fwmark already set by other firewall rules so keen-pbr does not process them again or change their route.",
        processRouterTrafficLabel: "Process router's own traffic",
        processRouterTrafficHint:
          "Apply route rules to, and learn domains from, traffic generated by the router itself. When disabled, only forwarded LAN traffic is routed by rules; DNS detour still applies to the router's own DNS queries.",
        clearDynamicSetsOnApplyLabel: "Clear learned domain addresses on apply",
        clearDynamicSetsOnApplyHint:
          "Clear dynamic ipset entries learned from DNS responses and L7 when applying a new configuration or restarting keen-pbr. Disable to preserve addresses until their TTL expires.",
        ipv6EnabledLabel: "Enable IPv6 support",
        ipv6EnabledHint:
          "Install IPv6 firewall sets and learn IPv6 destinations. Disable this on older firmware without IPv6 netfilter support.",
        inboundInterfacesLabel: "Processed (inbound) interfaces",
        inboundInterfacesHint:
          "Apply route rules only to the interfaces selected above. Prefer LAN interfaces and local VPN server interfaces to avoid changing routes for packets arriving from WAN. Leave empty to process traffic from any interface.",
        inboundInterfacesAddAction: "Add interface",
        inboundInterfacesLoading: "Loading interfaces...",
        inboundInterfacesNoAvailable: "No more interfaces available.",
        inboundInterfacesEmptyTitle: "No inbound interfaces selected",
        inboundInterfacesEmptyDescription:
          "Add interfaces here if you want policy routing to apply only to specific ingress interfaces.",
        inboundInterfacesLoadError:
          "Live interface inventory is temporarily unavailable. Saved selections are still editable.",
        inboundInterfacesStatusUp: "UP",
        inboundInterfacesStatusDown: "DOWN",
        inboundInterfacesStatusLoading: "Loading",
        inboundInterfacesStatusMissing: "Missing",
        inboundInterfacesMissingDetail:
          "This interface is saved in config but is not present in the current live interface inventory.",
      },
      intercept: {
        title: "Domain-based routing",
        description:
          "Configure how keen-pbr determines domain IP addresses to populate ipset.",
        enabledLabel: "Enable domain-based routing",
        enabledHint:
          "When disabled, keen-pbr neither intercepts DNS nor analyzes L7. Domain-based rules will not work, but IP/CIDR-based rules continue to work.",
        minTtlLabel: "Minimum TTL (seconds)",
        minTtlHint: "Minimum lifetime of entries in ipset.",
        maxTtlLabel: "Maximum TTL (seconds)",
        maxTtlHint: "Maximum lifetime of entries in ipset.",
        dnsEnabledLabel: "Intercept DNS server responses",
        dnsEnabledHint:
          "Intercept unencrypted DNS server responses on port 53, inspect the domain, and add IP addresses from the response to ipset when they match lists.",
        queueLabel: "NFQUEUE number",
        queueHint: "Netlink NFQUEUE queue number for intercepting DNS packets.",
        holdTimeoutLabel: "DNS hold timeout (milliseconds)",
        holdTimeoutHint:
          "How long keen-pbr may hold a DNS response to populate ipset (5–500 ms). A timeout that is too short may let client packets take the wrong route before the IP is added. A timeout that is too long may make websites feel slow to open if keen-pbr hangs or crashes and DNS responses are delayed.",
        markerDomainLabel: "Marker domain",
        markerDomainHint: "Domain answered by the synthetic DNS marker.",
        markerAddressLabel: "Marker IPv4 address",
        markerAddressHint: "IPv4 address returned for the marker domain.",
        l7EnabledLabel: "Enable L7 interception",
        l7EnabledHint:
          "Analyze TLS SNI, HTTP, and QUIC packets to identify domains and populate ipset. Helps route applications using their own DoH/DoT or hardcoded IPs when the domain name is visible in L7. When a newly observed IP is first added to ipset, keen-pbr deletes conntrack entries for that client and destination. The connection may be interrupted; reconnecting traffic then follows the correct route.",
        nflogGroupLabel: "NFLOG group",
        nflogGroupHint:
          "Netlink NFLOG group number for TLS SNI / HTTP / QUIC analysis.",
        tlsLabel: "TLS SNI",
        httpLabel: "HTTP Host",
        quicLabel: "QUIC",
      },
      autoupdate: {
        title: "Lists autoupdate",
        description: "Keep your remote lists up to date automatically.",
        enabledLabel: "Enable lists autoupdate",
        enabledHint:
          "Automatically download the latest version of your remote lists and update routing when they change.",
        cronLabel: "Refresh schedule",
        cronHintPrefix: "How often to check for updates. Uses cron format. Use",
        cronHintSuffix: "for help.",
        openInGuru: "Open in Crontab Guru",
      },
      advanced: {
        title: "Advanced routing settings",
        description:
          "Advanced settings - only change these if you know what you're doing.",
        fwmarkStartLabel: "Firewall mark starting value",
        fwmarkStartHint:
          "The starting fwmark assigned to your first outbound. Each additional outbound gets the next value in the range.",
        fwmarkMaskLabel: "Firewall mark mask",
        fwmarkMaskHintPrefix:
          "Bitmask defining which bits are used for fwmarks. Must be a continuous block of hex",
        fwmarkMaskHintSuffix: "digits, e.g.",
        tableStartLabel: "IP routing table starting value",
        tableStartHint:
          "The routing table ID assigned to your first outbound. Each additional outbound gets the next ID.",
        ipsetHashsizeLabel: "IPSet hash table size (ipset hashsize)",
        ipsetHashsizeHint:
          "Initial hash table size for address lookup in each ipset. Increasing it for large lists may reduce collisions and speed up lookup, but uses more RAM. This is not the entry limit. Changing this recreates ipsets and clears learned addresses.",
        ipsetMaxelemLabel: "Maximum entries in ipset (ipset maxelem)",
        ipsetIptablesOnlyHint: "This setting is only available for iptables",
        ipsetMaxelemHint:
          "Maximum IP addresses or subnets in each ipset. Increase for lists with many entries; increasing this limit uses more RAM. Changing this recreates ipsets and clears learned addresses.",
      },
      actions: {
        saving: "Saving...",
        save: "Save",
      },
    },
    dnsServers: {
      title: "DNS Servers",
      description: "Upstream DNS servers used for domain name resolution.",
      keeneticAddress: "Keenetic built-in DNS",
      actions: {
        add: "Add DNS server",
      },
      empty: {
        title: "No DNS servers yet",
        description: "Add a DNS server to configure upstream resolution.",
      },
      loadErrorDescription:
        "We can't load DNS servers right now. Try refreshing the page.",
      headers: {
        name: "Name",
        address: "Address",
        outbound: "Outbound",
        actions: "Actions",
      },
      delete: {
        confirmWithReferences:
          'DNS server "{{serverTag}}" is currently used by {{count}} rule(s){{fallbackSuffix}}.\nDelete and automatically remove those references?',
        fallbackSuffix: " and as fallback",
      },
      deleteDialog: {
        title: "Delete DNS servers?",
        description:
          "Confirming this operation will make the following changes:",
        confirm: "Delete",
        items: {
          serverPrefix: "DNS server",
          serverSuffix: "will be deleted.",
          dnsRule: "DNS rule #{{number}} will be deleted.",
          fallback: "Fallback DNS will be changed.",
        },
      },
      bulk: {
        selected: "{{count}} selected",
        delete: "Delete {{count}}",
        confirmDelete:
          "Delete DNS servers {{tags}}?\nAutomatically remove stale references?",
      },
      none: "none",
    },
    dnsServerUpsert: {
      createTitle: "Create DNS server",
      editTitle: "Edit DNS server",
      missingCardDescription: "The requested DNS server could not be found.",
      missingCardTitle: "Missing DNS server",
      missingDescription:
        "Return to the DNS servers table and choose a valid entry.",
      back: "Back to DNS servers",
      description:
        "This server will be available in your DNS rules and as a fallback.",
      cardDescription:
        "Choose the DNS server type and optional detour outbound.",
      editCardTitle: "Edit DNS server <entity>{{tag}}</entity>",
      fields: {
        tag: "Name",
        tagHint: "A short name for this server, used in DNS rules.",
        type: "DNS type",
        typeHint:
          "Keenetic reuses the router's current built-in DNS. Plaintext DNS uses a manually entered IP address.",
        typeOptions: {
          keenetic: "Keenetic DNS",
          static: "Plaintext DNS",
        },
        keeneticNotice: {
          description:
            "Configure DNS servers in the Keenetic web interface for this mode.",
          openLink: "Go to settings",
          navigation:
            "Go to Network Rules -> Internet safety -> DNS Configuration (Russian UI: Сетевые правила -> Интернет-фильтры -> Настройка DNS).",
          dotDohOnly:
            "If any DoT or DoH servers are configured there, only those servers will be used.",
        },
        address: "Address",
        addressPlaceholder: "1.1.1.1 or [2606:4700::1111]:53",
        addressHint:
          "The server's IP address, e.g. `1.1.1.1` or `[2606:4700::1111]:53`.",
        detour: "Make requests via Outbound",
        detourEmpty: "Not selected",
        detourPlaceholder: "Optional outbound tag",
        detourHint:
          "Optional: send DNS queries for this server through a specific outbound (e.g. a VPN).",
      },
      validation: {
        tagUnique: "Name must be unique.",
        typeRequired: "DNS type is required.",
        addressInvalid:
          "Address must be a valid IPv4/IPv6 value with an optional port.",
      },
      actions: {
        create: "Create DNS server",
        save: "Save DNS server",
      },
    },
    routingRules: {
      title: "Routing rules",
      description:
        "Rules that decide which outbound handles matching traffic. Evaluated top to bottom.",
      actions: {
        addRule: "Add routing rule",
        enableRule: "Enable rule",
        disableRule: "Disable rule",
      },
      messages: {
        saved: "Routing rules staged. Apply new config to persist them.",
      },
      bulk: {
        deleteConsequences:
          "After applying changes, the selected rules will no longer determine traffic routes. Remaining rules and router settings will be used.",
        selected: "{{count}} selected",
        enable: "Enable {{count}}",
        disable: "Disable {{count}}",
        delete: "Delete {{count}}",
        confirmDelete:
          "Delete {{count}} routing rule(s)? This cannot be undone from this screen alone.",
      },
      empty: {
        title: "No routing rules yet",
        description:
          "Add a routing rule to direct matching traffic to an outbound.",
      },
      headers: {
        order: "Order",
        criteria: "Match",
        outbound: "Outbound",
        runtime: "Runtime",
        actions: "Actions",
      },
      criteriaLabels: {
        lists: "Lists",
        proto: "Proto",
        dscp: "DSCP",
        sourceIp: "Source IP",
        destinationIp: "Destination IP",
        sourcePort: "Source port",
        destinationPort: "Destination port",
      },
    },
    routingRuleUpsert: {
      createTitle: "Create routing rule",
      editTitle: "Edit routing rule",
      editNamedTitle: "Edit routing rule <entity>#{{number}}</entity>",
      description:
        "This rule directs matching traffic to the specified outbound.",
      cardDescription:
        "Add conditions and choose an outbound for matching traffic.",
      builder: {
        title: "Conditions",
        description:
          "Add conditions one at a time. All added conditions must match.",
        chooseFirst: "Choose the first condition",
        addAnother: "Add another condition",
        choose: "Choose condition",
        available: "Available conditions",
        allAdded: "All available conditions have been added",
        if: "IF",
        and: "AND",
        then: "then",
        routeThrough: "Route traffic through",
        remove: "Remove condition “{{condition}}”",
        descriptions: {
          list: "The IP address or domain is in the specified lists",
          proto: "TCP or UDP",
          dscp: "Packet DSCP tag from 1 to 63.",
          src_port: "Source port of the connection.",
          dest_port: "Destination port of the connection.",
          src_addr: "Source IP addresses or subnets.",
          dest_addr: "Destination IP addresses or subnets.",
        },
      },
      messages: {
        saved: "Routing rule staged. Apply new config to persist it.",
      },
      missing: {
        cardDescription: "The requested routing rule could not be found.",
        cardTitle: "Missing routing rule",
        description:
          "Return to the routing rules table and choose a valid entry.",
        back: "Back to routing rules",
      },
      validation: {
        atLeastOneCondition:
          "Specify at least one condition: list, DSCP, source/destination address, or source/destination port.",
        dscpRange: "DSCP must be an integer between 1 and 63.",
      },
      actions: { create: "Create rule", save: "Save rule" },
      fields: {
        enabled: "Enable rule",
        mode: "Rule type",
        ruleType: "Rule types",
        modeOptions: {
          normal: "Conditional routing",
          ipv4: "IPv4 default gateway",
          ipv6: "IPv6 default gateway",
        },
        modeHint:
          "Default-gateway rules match non-local traffic for one IP family and do not use other conditions.",
        lists: "Lists",
        listsPlaceholderDescription:
          "Add one or more configured list names to match for this rule.",
        noListsSelected: "No lists selected",
        listsHint: "Choose which of your lists this rule applies to.",
        proto: "Protocol",
        any: "Any",
        anyLower: "any",
        protocol: "Protocol",
        dscp: "DSCP",
        dscpHint: "Match packets with this DSCP tag. Leave empty for any.",
        sourcePort: "Source port",
        destinationPort: "Destination port",
        sourcePortHint:
          "Source port(s). Comma-separated, ranges allowed. Prefix `!` to negate.",
        destinationPortHint:
          "Destination port(s). Comma-separated, ranges allowed. Prefix `!` to negate.",
        sourceAddresses: "Source addresses",
        destinationAddresses: "Destination addresses",
        sourceAddressHint:
          "Source IP/CIDR. Comma-separated. Prefix `!` to negate.",
        destinationAddressHint:
          "Destination IP/CIDR. Comma-separated. Prefix `!` to negate.",
        outbound: "Outbound",
        selectOutbound: "Select outbound",
        configuredOutbounds: "Configured outbounds",
        outboundHint: "Which outbound should handle matching traffic.",
      },
      placeholders: {
        dscp: "46",
        sourcePort: "80,443 or 10000-20000",
        destinationPort: "443 or !53,123",
        sourceAddresses: "192.168.1.10,10.0.0.0/8",
        destinationAddresses: "2001:db8::1 or !203.0.113.0/24",
      },
    },
    outbounds: {
      title: "Outbounds",
      description: "Your configured outbounds and urltest groups.",
      actions: { new: "Add outbound" },
      bulk: {
        selected: "{{count}} selected",
        delete: "Delete {{count}}",
        confirmDelete:
          "Delete {{count}} outbound(s)? Dependencies are not validated until save.",
      },
      deleteDialog: {
        title: "Delete outbounds?",
        description:
          "Confirming this operation will make the following changes:",
        confirm: "Delete",
        items: {
          outboundPrefix: "Outbound",
          outboundSuffix: "will be deleted.",
          dependentOutboundPrefix: "Dependent urltest outbound",
          dependentOutboundSuffix: "will be deleted.",
          routingRule: "Routing rule #{{number}} will be removed.",
          ruleDetail: "{{label}}: {{value}}",
          dnsDetour: 'DNS server "{{server}}" will be changed.',
          urltestGroupChanged:
            'Group #{{group}} in test outbound "{{outbound}}" will be changed.',
          urltestGroupRemoved:
            'Group #{{group}} in test outbound "{{outbound}}" will be deleted.',
          groupOutbounds: "Outbounds",
        },
      },
      empty: {
        title: "No outbounds yet",
        description: "Add an outbound to start building routing behavior.",
      },
      headers: {
        tag: "Name",
        type: "Type",
        summary: "Details",
        runtime: "Runtime",
        actions: "Actions",
      },
      summary: {
        interface: "ifname={{value}}",
        gateway4: "gateway4={{value}}",
        gateway6: "gateway6={{value}}",
        table: "table={{value}}",
        urltestDefault: "outbounds={{outbounds}}",
        urltest: "url={{url}}, outbounds={{outbounds}}",
        icmptest: "outbounds={{candidates}}",
      },
      messages: {
        missingReference:
          'Outbound "{{outbound}}" references missing outbound tag "{{referenced}}".',
      },
    },
    outboundUpsert: {
      noAdditionalSettings: "This outbound type has no additional settings",
      typeHints: {
        interface: "Traffic leaves through the selected network interface",
        table: "Traffic is sent to an existing routing table (ip route table).",
        urltest:
          "Select the outbound with the lowest latency (measured by sending an HTTP request)",
        icmptest:
          "Select the outbound with the lowest latency (measured by sending an ICMP packet)",
        blackhole: "All traffic sent to this outbound is dropped.",
        ignore:
          "Traffic is not processed by keen-pbr rules and is routed according to the router settings.",
      },
      advanced: {
        probesTitle: "Probes and retries",
        circuitBreakerTitle: "Circuit breaker",
        hasError: "has errors",
        changed: "changed",
        default: "default",
      },
      conntrack: {
        label: "Existing connections on switch",
        hint: "What happens to established connections when a healthy outbound is replaced by a faster or higher-priority one. Connections through a failed outbound are always reset.",
        preserve: "Keep",
        delete: "Reset",
      },
      ladder: {
        phrase: {
          priority:
            "All traffic goes through the fastest working outbound of tier 1.",
          balance:
            "New connections are spread across the working outbounds of tier 1 according to their weights.",
          fallbackOne: "If none of them works, tier 2 is used.",
          fallbackMany:
            "If none of them works, tiers 2–{{last}} are tried in order.",
        },
        primaryStep: "Primary tier",
        backupStep: "Backup tier {{index}}",
        activeNow: "in use now",
        ifAllDown: "if all are unavailable",
        moveUp: "Move up",
        moveDown: "Move down",
        removeStep: "Remove tier",
        removeMember: "Remove {{tag}}",
        add: "Add",
        addStep: "Add backup tier",
        noOptions: "All available outbounds are already used",
        weight: "Weight",
        weightHint:
          "Share of new connections for this outbound within the tier. For example, weights 7 and 3 give 70% and 30%. Empty means 1. Only working outbounds count: if one is down, its share is split among the others by their weights. Established connections stay where they are. Allowed range: 1 to 100.",
        shareTitle:
          "Share of new connections while every outbound of the tier works",
        pingTarget: "Ping target",
        latency: "{{value}} ms",
        roles: {
          selected: "selected",
          balanced: "in rotation",
          standby: "standby",
          waiting: "waiting",
          degraded: "degraded",
          unavailable: "unavailable",
        },
      },
      createTitle: "Create outbound",
      editTitle: "Edit outbound",
      editCardTitle: "Edit outbound <entity>{{tag}}</entity>",
      description:
        "An outbound can be a single network interface, a routing table, or a urltest group that picks the fastest option.",
      cardDescription: "Configure interface or urltest outbounds.",
      missing: {
        cardDescription: "The requested outbound could not be found.",
        cardTitle: "Missing outbound",
        description: "Return to the outbounds table and choose a valid entry.",
        back: "Back to outbounds",
      },
      actions: { create: "Create outbound", save: "Save outbound" },
      strategy: {
        cards: {
          priority: {
            title: "Fastest",
            description: "Picks the outbound with the lowest latency",
          },
          balance: {
            title: "Multipath",
            description:
              "Balances connections across the outbounds of the active tier",
          },
        },
        label: "Selection strategy",
        hint: "Priority keeps one selected outbound; balance distributes new connections across healthy outbounds (not available on Keenetic).",
        hintKeenetic:
          "Load balancing is disabled on Keenetic; use the router's multipath features.",
        options: {
          priority: "Priority",
          balance: "Balance",
        },
      },
      fields: {
        aboutType: "About this type",
        tag: "Name",
        type: "Type",
        outboundTypes: "Outbound types",
        typeOptions: {
          interface: "Interface",
          table: "Routing table",
          urltest: "Auto-select (urltest)",
          icmptest: "Auto-select (ICMP)",
          blackhole: "Blackhole",
          ignore: "Ignore",
        },
      },
      interface: {
        gatewayPlaceholder: "optional, e.g. auto or 10.23.0.1",
        gateway6Placeholder: "optional, e.g. auto or fe80::1",
        gatewaysHint: 'Enter "auto" to try to detect it automatically',
        title: "Interface settings",
        description:
          "Set the egress interface and optional IPv4/IPv6 gateways for this outbound.",
        interface: "Interface",
        interfacePlaceholder: "Select or type an interface",
        interfaceHint: "Egress interface name, e.g. `tun0`, `eth0`, `wg0`.",
        gateway: "Default gateway IPv4",
        gatewayHint:
          "Optional IPv4 gateway; use `auto` to discover it from the main default route.",
        gateway6: "Default gateway IPv6",
        gateway6Hint:
          "Optional IPv6 gateway; use `auto` to discover it from the main default route.",
      },
      table: {
        title: "Routing table settings",
        description: "Map this outbound to an existing kernel routing table.",
        field: "Table ID",
        hint: "Kernel routing table ID for this outbound.",
      },
      blackhole: {
        title: "Blackhole behavior",
        description:
          "Blackhole outbounds intentionally drop all matching traffic.",
      },
      ignore: {
        title: "Ignore behavior",
        description:
          "Ignore outbounds pass matching traffic through without policy-based routing changes.",
      },
      urltest: {
        probeTimeout: "Probe timeout (ms)",
        probeTimeoutHint:
          "How long to wait for each probe request (in milliseconds).",
        groupsTitle: "Outbound groups (urltest)",
        groupsDescription:
          "Add outbounds to this group. The fastest responding outbound (by urltest probe) will be selected.",
        groupTitle: "Group {{index}}",
        groupDescription:
          "Priority {{index}} - higher priority groups are preferred.",
        interfaceOutbounds: "Interface outbounds",
        addOutbound: "Add outbound",
        noInterfaceOutbounds: "No interface outbounds found.",
        addInterfaceOutboundsFirst:
          "Add interface outbounds first so urltest groups have selectable targets.",
        addGroup: "Add group",
        probingTitle: "Probing and retries",
        probingDescription:
          "Configure how the urltest group probes candidates and retries failed checks.",
        probeUrl: "Probe URL",
        probeUrlHint:
          "The service fetches this URL at the configured interval to verify the interface is alive and measure latency.",
        interval: "Interval (ms)",
        intervalHint: "How often to request the Probe URL (in milliseconds).",
        tolerance: "Tolerance (ms)",
        toleranceHint:
          "Don't switch outbounds unless the latency difference exceeds this value. Prevents flapping.",
        retryAttempts: "Retry attempts",
        retryAttemptsHint:
          "Extra probe attempts before marking the outbound as failed.",
        retryInterval: "Retry interval (ms)",
        retryIntervalHint:
          "Delay between retries after a failed probe (in milliseconds).",
      },
      icmptest: {
        title: "ICMP probing",
        description:
          "Configure strict ICMP echo checks. Each candidate must have its own literal IPv4 or IPv6 target.",
        targetHint:
          "Literal IPv4 or IPv6 address pinged through this outbound.",
        targetLabel: "Ping target for {{outbound}}",
        targetsEmpty:
          "Add candidates to an outbound group to configure their targets.",
        count: "Packets per run",
        countHint: "1–10 sequential ICMP attempts for each candidate.",
        maxFailed: "Allowed failed packets",
        maxFailedHint:
          "The candidate succeeds only when failures do not exceed this value.",
        packetInterval: "Pause between attempts (ms)",
        packetIntervalHint:
          "Wait after an attempt finishes before starting the next one (100–1000 ms).",
        probeTimeout: "Reply timeout (ms)",
        probeTimeoutHint:
          "Maximum wait for each strictly matched echo reply (100–5000 ms).",
        maxRtt: "Maximum accepted RTT (ms)",
        maxRttHint: "Slower matched replies count as failed packets.",
        interval: "Sweep interval (ms)",
        intervalHint:
          "Must cover the full worst-case sequential sweep plus a 25% reserve.",
        tolerance: "Selection tolerance (ms)",
        toleranceHint:
          "Keep the current candidate while it remains within this RTT of the best result.",
      },
      circuitBreaker: {
        title: "Circuit breaker - limit probing on persistent failure",
        description:
          "Prevents excessive probing when an interface or probe URL is persistently unavailable.",
        failures: "Failures before open",
        failuresHint: "Open the circuit after this many consecutive failures.",
        successes: "Successes to close",
        successesHint: "Successful probes required to close the circuit again.",
        timeout: "Open timeout (ms)",
        timeoutHint:
          "How long the circuit stays open before half-open probing begins (in ms).",
        halfOpen: "Half-open probes",
        halfOpenHint:
          "Number of probe attempts allowed during the half-open phase before the circuit fully closes or reopens.",
      },
      killSwitch: {
        title: "Kill-switch",
        description:
          "What happens to traffic routed to this outbound while its interface or gateway is down.",
        inheritNow: "Currently: {{value}}",
        options: {
          inherit: { title: "Default behavior" },
          off: {
            title: "Do not block",
            description:
              "Traffic bypasses the outbound and leaves via the main route.",
          },
          reject: {
            title: "Block with an error",
            description:
              "Connections fail immediately (unreachable): apps notice at once.",
          },
          drop: {
            title: "Block silently",
            description:
              "Packets are dropped (blackhole): apps wait for a timeout.",
          },
        },
        badge: {
          inherit: "default",
          off: "not blocking",
          reject: "block with error",
          drop: "block silently",
        },
      },
      validation: {
        duplicateTag: 'Outbound tag "{{tag}}" already exists.',
        missingReference:
          'Outbound "{{outbound}}" references missing outbound tag "{{referenced}}".',
      },
    },
    dnsRules: {
      title: "DNS Rules",
      description:
        "Control which DNS server is used for domains in your lists.",
      actions: {
        add: "Add DNS rule",
        enableRule: "Enable rule",
        disableRule: "Disable rule",
      },
      bulk: {
        deleteConsequences:
          "After applying changes, the selected rules will no longer select DNS servers for domains. Remaining rules and default DNS servers will be used.",
        selected: "{{count}} selected",
        enable: "Enable {{count}}",
        disable: "Disable {{count}}",
        delete: "Delete {{count}}",
        confirmDelete: "Delete {{count}} DNS rule(s)?",
      },
      messages: {
        saved: "DNS configuration staged. Apply new config to persist it.",
      },
      validation: {
        invalidFallback:
          "Primary DNS servers must reference existing server tags.",
        invalidFallbackChange:
          "Cannot change fallback while DNS rules are invalid.",
        invalidResult: "Cannot save because resulting DNS rules are invalid.",
      },
      fallback: {
        title: "Default upstream DNS servers",
        description:
          "The ordered DNS servers dnsmasq should use when no DNS rule matches. When set, dnsmasq ignores the system upstreams (no-resolv).",
        add: "Add fallback DNS server",
        placeholderTitle: "No default upstream DNS servers selected",
        placeholderDescription:
          "Optional. Leave empty to keep the system upstreams for domains not matched by any rule.",
        noneDefined: "No DNS servers defined on the DNS Servers page.",
        noneAvailable: "All DNS servers are already selected.",
      },
      integration: {},
      empty: {
        title: "No DNS rules yet",
        description:
          "No rules yet - add a rule to route DNS lookups for specific lists through a chosen server.",
      },
      headers: {
        criteria: "Match",
        serverTag: "DNS server",
        allowDomainRebinding: "Private IPs (rebind)",
        actions: "Actions",
      },
      criteriaLabels: {
        lists: "Lists",
      },
      rebinding: {
        enabled: "Private IPs allowed",
        disabled: "Private IPs blocked",
      },
    },
    dnsRuleUpsert: {
      createTitle: "Create DNS rule",
      editTitle: "Edit DNS rule",
      editNamedTitle: "Edit DNS rule <entity>#{{number}}</entity>",
      description:
        "This rule defines which DNS server to use for domains in a specific list.",
      cardDescription: "Set the list names and DNS server for this rule.",
      messages: { saved: "DNS rule staged. Apply new config to persist it." },
      validation: {
        notFound: "The requested DNS rule was not found.",
        fixErrors: "Fix validation errors before saving.",
        serverRequired: "Rule must reference an existing DNS server.",
        unknownLists: "Unknown lists: {{lists}}",
        duplicate: "Duplicate rule entry.",
      },
      missing: {
        cardDescription: "The requested DNS rule could not be found.",
        cardTitle: "Missing DNS rule",
        description: "Return to DNS Rules and choose a valid entry.",
        back: "Back to DNS rules",
      },
      actions: { create: "Create rule", save: "Save rule" },
      fields: {
        serverTag: "DNS server",
        selectServer: "Select DNS server",
        dnsServers: "DNS servers",
        noServers: "No DNS servers defined on the DNS Servers page.",
        listNames: "Domain lists",
        allowDomainRebinding: "Allow domain rebinding for these domains",
        allowDomainRebindingHint:
          "Enable this only when you know this domain list points to internal services. Responses for matched domains will be allowed to contain internal/private IPs (for example 192.168.0.0/16, 10.0.0.0/8, and other local network ranges).",
        listPlaceholderDescription:
          "Choose which lists this rule applies to. Matching domains will use this DNS server.",
        noListsSelected: "No lists selected",
        noLists:
          "No lists found. Please, create first filter on the Lists page.",
      },
    },
    lists: {
      title: "Lists",
      description:
        "Groups of domains and IP addresses you can use in your traffic and DNS rules.",
      actions: {
        new: "Add list",
        update: "Update",
        updateAll: "Update all",
      },
      empty: {
        title: "No lists yet",
        description:
          "Create your first list to use it in routing and DNS rules.",
      },
      headers: {
        name: "Name",
        type: "Type",
        stats: "Domains / IPv4 / IPv6",
        rules: "Used in rules",
        actions: "Actions",
      },
      delete: {
        confirm: 'Delete list "{{name}}"?',
        confirmWithReferences:
          'Delete list "{{name}}" and remove its references from routing and DNS rules?',
      },
      deleteDialog: {
        title: "Delete lists?",
        description:
          "Confirming this operation will make the following changes:",
        confirm: "Delete",
        items: {
          listPrefix: "List",
          listSuffix: "will be deleted.",
          routeRuleRemoved: "Routing rule #{{number}} will be deleted.",
          routeRuleUpdated: "Routing rule #{{number}} will be changed.",
          dnsRuleRemoved: "DNS rule #{{number}} will be deleted.",
          dnsRuleUpdated: "DNS rule #{{number}} will be changed.",
        },
      },
      bulk: {
        selected: "{{count}} selected",
        refreshSelected: "Update {{count}} (URL)",
        deleteSelected: "Delete {{count}}",
        confirmDeleteSimple: "Delete lists: {{names}}?",
        confirmDeleteWithRefs:
          "Delete lists: {{names}} and remove references from routing/DNS rules where needed?",
        noUrlBacked: "None of the selected lists are URL-backed.",
      },
      location: {
        inline: "Inline",
      },
      refresh: {
        draftBlocked: "Apply draft config before updating lists.",
        updateDisabled: "Apply the staged draft before refreshing",
      },
      rule: {
        used_one: "Used in {{count}} rule",
        used_other: "Used in {{count}} rules",
      },
      messages: {
        refreshedOne: "List refresh finished.",
        refreshedAll: "Lists refresh finished.",
        refreshFailedOne:
          'List "{{names}}" was not updated. See logs for details.',
        refreshFailedMany:
          "{{count}} lists were not updated: {{names}}. See logs for details.",
        refreshFailedMore: "+{{count}} more",
      },
      lastUpdated: "Last updated: {{value}}",
      neverUpdated: "Never updated",
      noStats: "-",
      source: {
        url: "URL",
        file: "File",
        domains: "Domains",
        ip_cidrs: "IP CIDRs",
        empty: "Empty",
      },
    },
    listUpsert: {
      createTitle: "Create list",
      editTitle: "Edit list",
      editCardTitle: "Edit list <entity>{{name}}</entity>",
      fallbackName: "list",
      description:
        "A list can contain domains and IPs you enter directly, load from a URL, or import from a file.",
      cardDescription:
        "Review the list source, TTL, and matching entries before saving.",
      messages: {
        created: "List staged. Apply new config to persist it.",
        updated: "List changes staged. Apply new config to persist them.",
      },
      missing: {
        cardDescription: "The requested list could not be found.",
        cardTitle: "Missing list",
        description: "Return to the lists table and choose a valid entry.",
        back: "Back to lists",
      },
      actions: {
        saving: "Saving...",
        create: "Create list",
        save: "Save list",
      },
      common: {
        title: "List settings",
        description: "Set the list identity before choosing the source.",
      },
      sourceSwitcher: {
        confirmTitle: "Change source type?",
        confirmAction: "Change source",
        title: "Source type",
        description:
          "Choose which source to edit. Legacy lists with multiple saved sources stay visible until you switch.",
        confirmChange:
          "The populated data of the listed sources will be cleared from the form. The change takes effect after saving the list.",
      },
      sourceGroups: {
        url: {
          button: "URL",
          title: "Remote URL",
          description:
            "Load list entries from a remote HTTP or HTTPS endpoint and control the cache lifetime for resolved IPs.",
        },
        file: {
          button: "File on device",
          title: "Local file",
          description: "Read list entries from a file available on the router.",
        },
        inline: {
          button: "Domains / IPs",
          title: "Domains / IPs",
          description: "Enter domains and IPs directly in the config.",
        },
      },
      fields: {
        name: "Name",
        ttlMs: "IP cache duration (ms)",
        ttlMsHint:
          "How long to keep resolved IPs in the ipset. `0` = no timeout.",
        detour: "Make requests via Outbound",
        detourEmpty: "Not selected",
        detourPlaceholder: "Optional outbound tag",
        detourHint:
          "Optional outbound to use when downloading this list from a remote URL.",
        url: "Remote URL",
        urlHint:
          "Optional: a URL to download entries from. Combined with anything you add below.",
        file: "Absolute file path",
        fileHint:
          "Optional: a file path on the device to load entries from. Combined with other sources.",
        domains: "Domains",
        domainsHint:
          "Domains to include, one per line. `example.com` will also match all subdomains.",
        ipCidrs: "IP CIDRs",
        ipCidrsHint:
          "IP addresses or CIDR ranges, one per line. E.g. <code>93.184.216.34</code>, <code>10.0.0.0/8</code>.",
      },
      validation: {
        inlineRequired: "Enter domains or IP/CIDR entries",
        duplicateName: "A list with this name already exists.",
        invalidTtl: "TTL must be a non-negative integer.",
      },
    },
  },
} as const
