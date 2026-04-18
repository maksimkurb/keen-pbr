export const enTranslation = {
      common: {
        language: "Language",
        theme: "Theme",
        close: "Close",
        cancel: "Cancel",
        copy: "Copy",
        copied: "Copied",
        clipboardUnavailable: "Clipboard unavailable",
        edit: "Edit",
        delete: "Delete",
        moveUp: "Move up",
        moveDown: "Move down",
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
          internet: "Internet",
          networkRules: "Traffic Rules",
        },
        items: {
          systemMonitor: "Dashboard",
          settings: "Settings",
          outbounds: "Outbounds",
          dnsServers: "DNS Servers",
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
          applyingAndRestarting: "Applying & Restarting...",
          applyAndRestart: "Apply & Restart",
          restarting: "Restarting...",
          restart: "Restart",
        },
        compact: {
          keenRestartRequired: "Pending changes",
          keenRestartRequiredDescription:
            "New settings found. Apply to restart keen-pbr.",
          keenAndDnsmasqRestartRequired: "Out of sync",
          keenAndDnsmasqRestartRequiredDescription:
            "Apply changes to sync keen-pbr and dnsmasq.",
          dnsmasqRestartRequired: "DNS-server config is outdated",
          dnsmasqRestartRequiredDescription:
            "dnsmasq needs a restart to update its resolver config.",
          dnsmasqRestarting: "Restarting dnsmasq...",
          dnsmasqRestartingDescription:
            "dnsmasq is restarting. Please wait.",
          dnsmasqUnavailable: "dnsmasq is unavailable",
          dnsmasqUnavailableDescription:
            "dnsmasq is not responding. Internet connectivity may be affected. Try Apply & Restart, or disable keen-pbr to restore network access.",
          staleAfterTimeout:
            "dnsmasq last reloaded at {{actualTs}}. Restart routing runtime if this stays stale.",
        },
        full: {
          unsavedTitle: "Configuration is unsaved",
          staleTitle: "dnsmasq is using a stale resolver config",
          staleDescription:
            "The expected resolver hash ({{expected}}…) doesn't match dnsmasq's active hash ({{actual}}…).",
        },
      },
      overview: {
        pageDescription:
          "Overview of routing runtime, config state, and active outbounds",
        runtime: {
          title: "Routing runtime",
          description: "Control policy-based routing.",
          loadError: "Failed to load routing runtime state.",
          version: "Version",
          router: "Router",
          status: "Routing status",
          dnsmasqHealthy: "dnsmasq healthy",
          dnsmasqWaiting: "dnsmasq reloading",
          dnsmasqStale: "dnsmasq restart required",
          dnsmasqUnavailable: "dnsmasq unavailable",
          dnsmasqUnknown: "dnsmasq status unknown",
          actions: {
            start: "Start",
            stop: "Stop",
            restart: "Restart",
          },
        },
        outbounds: {
          title: "Outbounds health",
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
          button: "Download diagnostics file",
          modal: {
            title: "Warning: sensitive data",
            description: "The diagnostics file includes:",
            items: {
              config:
                "Your full configuration file (including the lists in use)",
              serviceHealth: "Service health",
              routingHealth: "Routing health",
              outbounds: "Outbounds status",
              names: "Names of lists, outbounds, and interfaces",
            },
            trustWarning:
              "Please share this file only with people you trust.",
            hideListsOption:
              "Hide list contents and list URLs",
            downloadAction: "Download diagnostics file",
          },
        },
        dnsCheck: {
          card: {
            title: "DNS check",
            description:
              "Verifies that DNS resolution through keen-pbr is working correctly from this browser or another device.",
            disabledDescription:
              "Enable `config.dns.dns_test_server` to run the built-in DNS self-check.",
            configuredServers: "Configured DNS servers",
            noServers:
              "No upstream DNS servers are configured in `config.dns.servers`.",
            via: "via {{detour}}",
            checking: "Checking...",
            runAgain: "Run again",
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
            disabled: "Built-in DNS probe is disabled in config.",
            browserSuccess: "DNS request from the browser reached dnsmasq.",
            manualProbeSuccess: "DNS request from the device reached dnsmasq.",
            browserProbeFail:
              "Browser request completed, but the DNS probe did not see the lookup.",
            sseUnavailable:
              "The live DNS event stream is unavailable, so the check could not start.",
            browserFail:
              "Browser request ran, but the DNS lookup was not observed.",
            sseFail: "Live DNS event stream is not connected.",
            browserChecking: "Checking browser DNS path...",
            browserUnknown: "Browser DNS status is not known yet.",
            manualSuccess: "DNS request from the device reached dnsmasq.",
            manualWaiting: "Waiting for your manual nslookup command...",
            manualIncomplete: "Manual device test has not completed yet.",
          },
        },
        routingTest: {
          title: "Where does this traffic go?",
          placeholder: "e.g. google.com or 1.2.3.4",
          submit: "Check route",
          invalidTarget: "Please enter a valid domain or IP.",
          requestFailed: "Routing test failed. Please try again.",
          emptyTitle: "No route matched",
          emptyDescription: "Try another domain or IP address.",
        },
        routingDiagnostics: {
          noMatchingRule: "No matching routing rule for the target lists.",
          hostLabel: 'Host "{{target}}"',
          inRuleLists: "In rule domain/IP lists?",
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
      pages: {
        settings: {
          title: "Settings",
          description:
            "Global defaults that apply to all your outbounds and rules.",
          saved: "Settings staged. Apply new config to persist them.",
          general: {
            title: "General",
            description: "Default behavior for all outbounds.",
            strictEnforcementLabel:
              "Block traffic when outbound drops (kill-switch)",
            strictEnforcementHint:
              "If a VPN or interface goes offline, traffic matching its rules is blocked instead of falling back to the main routing table. Can be overridden per outbound.",
            inboundInterfacesLabel: "Inbound interfaces",
            inboundInterfacesHint:
              "Only packets arriving on the selected interfaces will be processed by policy routing. Leave this empty to match traffic from any interface.",
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
          autoupdate: {
            title: "Lists autoupdate",
            description: "Keep your remote lists up to date automatically.",
            enabledLabel: "Enable lists autoupdate",
            enabledHint:
              "Automatically download the latest version of your remote lists and update routing when they change.",
            cronLabel: "Refresh schedule",
            cronHintPrefix:
              "How often to check for updates. Uses cron format - use",
            cronHintSuffix: "if you need help.",
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
          editCardTitle: "Edit {{tag}}",
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
            detour: "Outbound",
            detourEmpty: "Not selected",
            detourPlaceholder: "Optional outbound tag",
            detourHint:
              "Optional: send DNS queries for this server through a specific outbound (e.g. a VPN).",
          },
          validation: {
            tagRequired: "Name is required.",
            tagUnique: "Name must be unique.",
            typeRequired: "DNS type is required.",
            addressRequired: "Address is required.",
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
          empty: {
            title: "No routing rules yet",
            description:
              "Add a routing rule to direct matching traffic to an outbound.",
          },
          headers: {
            enabled: "Enabled",
            order: "Order",
            criteria: "Match",
            outbound: "Outbound",
            runtime: "Runtime",
            actions: "Actions",
          },
          criteriaLabels: {
            lists: "Lists",
            proto: "Proto",
            sourceIp: "Source IP",
            destinationIp: "Destination IP",
            sourcePort: "Source port",
            destinationPort: "Destination port",
          },
        },
        routingRuleUpsert: {
          createTitle: "Create routing rule",
          editTitle: "Edit routing rule",
          description:
            "This rule directs matching traffic to the specified outbound.",
          cardDescription:
            "Choose lists and outbound, then optionally narrow by protocol, ports, and addresses.",
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
              "Specify at least one condition: list, source/destination address, or source/destination port.",
            outboundRequired: "Outbound tag is required.",
          },
          actions: { create: "Create rule", save: "Save rule" },
          fields: {
            lists: "Lists",
            listsPlaceholderDescription:
              "Add one or more configured list names to match for this rule.",
            noListsSelected: "No lists selected",
            listsHint: "Choose which of your lists this rule applies to.",
            proto: "Proto",
            any: "Any",
            anyLower: "any",
            protocol: "Protocol",
            protoHint: "Filter by protocol (TCP, UDP, etc.). Leave empty for any.",
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
            table: "table={{value}}",
            urltest: "outbounds={{value}}",
          },
          messages: {
            missingReference:
              'Outbound "{{outbound}}" references missing outbound tag "{{referenced}}".',
          },
        },
        outboundUpsert: {
          createTitle: "Create outbound",
          editTitle: "Edit outbound",
          editCardTitle: "Edit {{tag}}",
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
          common: {
            noExtraFields:
              "No additional fields are required for this type beyond the outbound tag.",
          },
          fields: {
            tag: "Name",
            tagHint:
              "A unique name for this outbound. Referenced in traffic rules and groups.",
            type: "Type",
            outboundTypes: "Outbound types",
            typeOptions: {
              interface: "Interface",
              table: "Routing table",
              urltest: "Auto-select (urltest)",
              blackhole: "Blackhole",
              ignore: "Ignore",
            },
          },
          interface: {
            title: "Interface settings",
            description:
              "Set the egress interface and optional gateway for this outbound.",
            interface: "Interface",
            interfaceHint: "Egress interface name, e.g. `tun0`, `eth0`, `wg0`.",
            gateway: "Gateway",
            gatewayHint: "Optional gateway IP for this outbound.",
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
          strictEnforcement: {
            label: "Kill-switch override",
            hint: "Override the global kill-switch setting for this outbound.",
            default: "Default (as in global config)",
            enabled: "Enabled",
            disabled: "Disabled",
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
            title: "Primary DNS servers",
            description:
              "The ordered DNS servers dnsmasq should use when no DNS rule matches.",
            add: "Add primary DNS server",
            placeholderTitle: "No primary DNS servers selected",
            placeholderDescription:
              "Add one or more DNS servers. The order is preserved and used in generated dnsmasq config.",
            noneDefined: "No DNS servers defined in config.dns.servers.",
            noneAvailable: "All DNS servers are already selected.",
          },
          empty: {
            title: "No DNS rules yet",
            description:
              "No rules yet - add a rule to route DNS lookups for specific lists through a chosen server.",
          },
          headers: {
            enabled: "Enabled",
            lists: "Lists",
            serverTag: "DNS server",
            allowDomainRebinding: "Domain rebinding",
            actions: "Actions",
          },
          rebinding: {
            enabled: "Allowed",
            disabled: "Blocked",
          },
        },
        dnsRuleUpsert: {
          createTitle: "Create DNS rule",
          editTitle: "Edit DNS rule",
          description:
            "This rule defines which DNS server to use for domains in a specific list.",
          cardDescription: "Set the list names and DNS server for this rule.",
          messages: { saved: "DNS rule staged. Apply new config to persist it." },
          validation: {
            notFound: "The requested DNS rule was not found.",
            fixErrors: "Fix validation errors before saving.",
            serverRequired: "Rule must reference an existing DNS server.",
            listsRequired: "Rule must include at least one list name.",
            unknownLists: "Unknown list names: {{lists}}",
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
            listNames: "List names",
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
            stats: "Entries",
            rules: "Used in rules",
            actions: "Actions",
          },
          delete: {
            confirm: 'Delete list "{{name}}"?',
            confirmWithReferences:
              'Delete list "{{name}}" and remove its references from routing and DNS rules?',
          },
          location: {
            inline: "Inline",
          },
          refresh: {
            draftBlocked:
              "Apply or discard the staged draft before refreshing URL-backed lists.",
            updateDisabled: "Apply or discard the staged draft before refreshing",
          },
          rule: {
            configured: "Configured",
          },
          messages: {
            refreshedOne: "List refresh finished.",
            refreshedAll: "Lists refresh finished.",
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
          editCardTitle: "Edit {{name}}",
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
            title: "Source type",
            description:
              "Choose which source to edit. Legacy lists with multiple saved sources stay visible until you switch.",
            confirmChange:
              "Switch source type and clear the currently filled fields?",
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
            nameHint: "Stable identifier used in rules and references.",
            ttlMs: "IP cache duration (ms)",
            ttlMsHint:
              "How long to keep resolved IPs in the ipset. `0` = no timeout.",
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
              "IP addresses or CIDR ranges, one per line. E.g. `93.184.216.34`, `10.0.0.0/8`.",
          },
          validation: {
            nameRequired: "Name is required.",
            duplicateName: "A list with this name already exists.",
            invalidTtl: "TTL must be a non-negative integer.",
          },
        },
      },
    } as const
