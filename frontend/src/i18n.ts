import i18n from "i18next"
import { initReactI18next } from "react-i18next"

export type Language = "en" | "ru"

export const DEFAULT_LANGUAGE: Language = "en"
export const LANGUAGE_STORAGE_KEY = "language"

const LANGUAGE_VALUES: Language[] = ["en", "ru"]

const resources = {
  en: {
    translation: {
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
        loadErrorDescription: "We can't load data right now. Try refreshing the page.",
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
          networkRules: "Network Rules",
        },
        items: {
          systemMonitor: "System monitor",
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
        compact: {
          draftPending: "Configuration draft pending save.",
          saving: "Saving...",
          apply: "Apply",
          resolverStale: "dnsmasq config is stale; reload required.",
          reloading: "Reloading...",
          reload: "Reload",
        },
        full: {
          unsavedTitle: "Configuration is unsaved",
          unsavedDescription:
            "Configuration has been staged in memory. Save and apply it to persist it to disk and reload the service.",
          applying: "Applying...",
          applyConfig: "Apply config",
          staleTitle: "dnsmasq is using a stale resolver config",
          staleDescription:
            "The expected resolver hash ({{expected}}…) doesn't match dnsmasq's active hash ({{actual}}…). Reload keen-pbr to regenerate and apply the current resolver configuration.",
          reloading: "Reloading...",
          reloadService: "Reload service",
        },
      },
      overview: {
        service: {
          title: "keen-pbr",
          loadError: "Failed to load service health.",
          unsupportedActionReason: "Not available in current API",
          version: "Version",
          status: "Service status",
          dnsmasqGood: "dnsmasq good",
          dnsmasqStale: "dnsmasq restart required",
          actions: {
            start: "Start",
            stop: "Stop",
            restart: "Restart",
            reload: "Reload",
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
          title: "Routing rule health",
          loadError: "Unable to load routing checks.",
          emptyTitle: "No routing checks reported yet",
          emptyDescription: "Routing checks will appear after the next reload.",
        },
        dnsCheck: {
          card: {
            title: "DNS server self-check",
            description:
              "Confirms that DNS lookups reach the built-in test server from the browser and from another device.",
            disabledDescription:
              "Enable `config.dns.dns_test_server` to run the built-in DNS self-check.",
            configuredServers: "Configured DNS servers",
            noServers: "No upstream DNS servers are configured in `config.dns.servers`.",
            via: "via {{detour}}",
            checking: "Checking...",
            runAgain: "Run again",
            testFromPc: "Test from PC",
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
            browserSuccess: "Browser DNS lookup reached the test server.",
            manualProbeSuccess: "Manual device DNS lookup reached the test server.",
            browserProbeFail:
              "Browser request completed, but the DNS probe did not see the lookup.",
            sseUnavailable:
              "The live DNS event stream is unavailable, so the check could not start.",
            browserFail: "Browser request ran, but the DNS lookup was not observed.",
            sseFail: "Live DNS event stream is not connected.",
            browserChecking: "Checking browser DNS path...",
            browserUnknown: "Browser DNS status is not known yet.",
            manualSuccess: "Manual device test reached the DNS probe.",
            manualWaiting: "Waiting for your manual nslookup command...",
            manualIncomplete: "Manual device test has not completed yet.",
          },
        },
      },
      pages: {
        routingRules: {
          title: "Routing rules",
          description: "Manage route rule order and field-level match criteria.",
          actions: { addRule: "Add rule" },
          messages: { saved: "Routing rules staged. Apply config to persist them." },
          empty: {
            title: "No routing rules yet",
            description: "Add a routing rule to direct matching traffic to an outbound.",
          },
          headers: {
            order: "Order",
            criteria: "Criteria",
            outbound: "Outbound",
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
          description: "Routing rules are matched in order and direct traffic to configured outbounds.",
          cardDescription: "Choose lists and outbound, then optionally narrow by protocol, ports, and addresses.",
          messages: { saved: "Routing rule staged. Apply config to persist it." },
          missing: {
            cardDescription: "The requested routing rule could not be found.",
            cardTitle: "Missing routing rule",
            description: "Return to the routing rules table and choose a valid entry.",
            back: "Back to routing rules",
          },
          validation: {
            selectList: "Select at least one list.",
            outboundRequired: "Outbound tag is required.",
          },
          actions: { create: "Create rule", save: "Save rule" },
          fields: {
            lists: "Lists",
            listsPlaceholderDescription: "Add one or more configured list names to match for this rule.",
            noListsSelected: "No lists selected",
            listsHint: "List names are sourced from config.lists keys.",
            proto: "Proto",
            any: "Any",
            anyLower: "any",
            protocol: "Protocol",
            protoHint: 'Use "any" by leaving proto empty.',
            sourcePort: "Source port",
            destinationPort: "Destination port",
            portHint: "Comma-separated ports or ranges. Optional leading ! to negate.",
            sourceAddresses: "Source addresses",
            destinationAddresses: "Destination addresses",
            addressHint: "Comma-separated IP addresses or CIDRs. Optional leading ! negates the entire spec.",
            outbound: "Outbound",
            selectOutbound: "Select outbound",
            configuredOutbounds: "Configured outbounds",
            outboundHint: "Outbound tags are sourced from config.outbounds.",
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
          description: "Configured outbounds and urltest behavior.",
          actions: { new: "New outbound" },
          empty: {
            title: "No outbounds yet",
            description: "Add an outbound to start building routing behavior.",
          },
          headers: {
            tag: "Tag",
            type: "Type",
            summary: "Summary",
            actions: "Actions",
          },
          summary: {
            interface: "ifname={{value}}",
            table: "table={{value}}",
            urltest: "outbounds={{value}}",
          },
          messages: {
            missingReference: 'Outbound "{{outbound}}" references missing outbound tag "{{referenced}}".',
          },
        },
        outboundUpsert: {
          createTitle: "Create outbound",
          editTitle: "Edit outbound",
          editCardTitle: "Edit {{tag}}",
          description: "Outbounds define direct interfaces or grouped urltest behavior.",
          cardDescription: "Configure interface or urltest outbounds.",
          missing: {
            cardDescription: "The requested outbound could not be found.",
            cardTitle: "Missing outbound",
            description: "Return to the outbounds table and choose a valid entry.",
            back: "Back to outbounds",
          },
          actions: { create: "Create outbound", save: "Save outbound" },
          common: { noExtraFields: "No additional fields are required for this type beyond the outbound tag." },
          fields: {
            tag: "Tag",
            tagHint: "Use a unique outbound tag that can be referenced by rules, groups, and detours.",
            type: "Type",
            outboundTypes: "Outbound types",
            typeHint: "Choose the outbound type defined by the config schema; the form below updates to show only relevant fields.",
          },
          interface: {
            title: "Interface settings",
            description: "Configure the egress device and optional gateway for interface-based routing.",
            interface: "Interface",
            interfaceHint: "Network interface name used for egress, such as tun0 or eth0.",
            gateway: "Gateway",
            gatewayHint: "Optional gateway IP address for this interface outbound.",
          },
          table: {
            title: "Table settings",
            description: "Map this outbound to an existing kernel routing table.",
            field: "Table ID",
            hint: "Kernel routing table ID required for the table outbound type.",
          },
          blackhole: {
            title: "Blackhole behavior",
            description: "Blackhole outbounds intentionally drop all matching traffic.",
          },
          ignore: {
            title: "Ignore behavior",
            description: "Ignore outbounds pass matching traffic through without policy-based routing changes.",
          },
          urltest: {
            groupsTitle: "Outbound groups",
            groupsDescription: "Groups are tried in order. Each group selects from interface outbounds, and order acts as priority.",
            groupTitle: "Group {{index}}",
            groupDescription: "Priority {{index}}. Earlier groups are preferred before later ones.",
            interfaceOutbounds: "Interface outbounds",
            addOutbound: "Add outbound",
            noInterfaceOutbounds: "No interface outbounds found.",
            addInterfaceOutboundsFirst: "Add interface outbounds first so urltest groups have selectable targets.",
            addGroup: "Add group",
            probingTitle: "Probing and retries",
            probingDescription: "Configure how the urltest group probes candidates and retries failed checks.",
            probeUrl: "Probe URL",
            probeUrlHint: "Health checks fetch this URL to measure availability and latency.",
            interval: "Interval (ms)",
            intervalHint: "Interval between probes.",
            tolerance: "Tolerance (ms)",
            toleranceHint: "If latency difference is not larger than this value, destination will not change.",
            retryAttempts: "Retry attempts",
            retryAttemptsHint: "Number of extra probe attempts before the check is treated as failed.",
            retryInterval: "Retry interval (ms)",
            retryIntervalHint: "Delay between retry attempts after a failed probe.",
          },
          circuitBreaker: {
            title: "Circuit breaker",
            description: "Fallback parameters when probing fails.",
            failures: "Failures before open",
            failuresHint: "Open the circuit after this many consecutive failed checks.",
            successes: "Successes to close",
            successesHint: "Close the circuit again after this many successful recovery probes.",
            timeout: "Open timeout (ms)",
            timeoutHint: "How long the circuit stays open before half-open probing resumes.",
            halfOpen: "Half-open probes",
            halfOpenHint: "Maximum concurrent probes allowed while testing recovery.",
          },
          strictEnforcement: {
            label: "Strict enforcement",
            hint: "Override the daemon-level strict routing setting for this interface outbound.",
            default: "Default (as in global config)",
            enabled: "Enabled",
            disabled: "Disabled",
          },
          validation: {
            duplicateTag: 'Outbound tag "{{tag}}" already exists.',
            missingReference: 'Outbound "{{outbound}}" references missing outbound tag "{{referenced}}".',
          },
        },
        dnsRules: {
          title: "DNS Rules",
          description: "Assign routing lists to specific DNS servers.",
          actions: { add: "Add DNS rule" },
          messages: { saved: "DNS configuration staged. Apply config to persist it." },
          validation: {
            invalidFallback: "Fallback server must reference an existing server tag.",
            invalidFallbackChange: "Cannot change fallback while DNS rules are invalid.",
            invalidResult: "Cannot save because resulting DNS rules are invalid.",
          },
          fallback: {
            title: "Fallback",
            description: "Used when no DNS rule matches the current request.",
            field: "Fallback server tag",
            placeholder: "Select a DNS server",
            group: "DNS servers",
            noneDefined: "No DNS servers defined in config.dns.servers.",
          },
          empty: {
            title: "No DNS rules yet",
            description: "Add a DNS rule to map configured lists to DNS servers.",
          },
          headers: {
            lists: "Lists",
            serverTag: "Server tag",
            actions: "Actions",
          },
        },
        dnsRuleUpsert: {
          createTitle: "Create DNS rule",
          editTitle: "Edit DNS rule",
          description: "DNS rules map configured lists to DNS server tags.",
          cardDescription: "Set the list names and DNS server for this rule.",
          messages: { saved: "DNS rule staged. Apply config to persist it." },
          validation: {
            notFound: "The requested DNS rule was not found.",
            fixErrors: "Fix validation errors before saving.",
          },
          missing: {
            cardDescription: "The requested DNS rule could not be found.",
            cardTitle: "Missing DNS rule",
            description: "Return to DNS Rules and choose a valid entry.",
            back: "Back to DNS rules",
          },
          actions: { create: "Create rule", save: "Save rule" },
          fields: {
            serverTag: "Server tag",
            selectServer: "Select DNS server",
            dnsServers: "DNS servers",
            noServers: "No DNS servers defined on the DNS Servers page.",
            listNames: "List names",
            listPlaceholderDescription: "Add one or more configured list names to match this DNS rule.",
            noListsSelected: "No lists selected",
            noLists: "No lists found. Please, create first filter on the Lists page.",
          },
        },
        lists: {
          title: "Lists",
          description: "Manage domain and IP lists used by routing and DNS rules.",
          actions: {
            new: "New list",
            update: "Update",
          },
          empty: {
            title: "No lists yet",
            description: "Create your first list to use it in routing and DNS rules.",
          },
          headers: {
            name: "Name",
            type: "Type",
            stats: "Stats",
            rules: "Rules",
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
          rule: {
            configured: "Configured",
          },
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
          description: "Lists can be backed by files, built-in sources, or remote URLs.",
          cardDescription: "Review the list source, TTL, and matching entries before saving.",
          messages: {
            created: "List staged. Apply config to persist it.",
            updated: "List changes staged. Apply config to persist them.",
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
          fields: {
            name: "Name",
            nameHint:
              "Use a stable identifier so rules and references remain easy to follow.",
            ttlMs: "TTL ms",
            ttlMsHint:
              "How long resolved IPs from domains in this list stay in the IP set; 0 means no timeout.",
            url: "Remote URL",
            urlHint:
              "Optional remote source loaded over HTTP or HTTPS and merged into the list.",
            file: "Local file",
            fileHint:
              "Optional local file path. File entries are merged with any inline domains, IPs, and remote URL data.",
            domains: "Domains",
            domainsHint:
              "Inline domain patterns. Writing example.com automatically includes all subdomains.",
            ipCidrs: "IP CIDRs",
            ipCidrsHint:
              "Inline IP addresses or CIDR ranges, for example 93.184.216.34, 10.0.0.0/8, or 2001:db8::/32.",
          },
          validation: {
            nameRequired: "Name is required.",
            duplicateName: "A list with this name already exists.",
            invalidTtl: "TTL must be a non-negative integer.",
            sourceRequired:
              "At least one source is required: remote URL, local file, domains, or IP CIDRs.",
          },
        },
      },
    },
  },
  ru: {
    translation: {
      common: {
        language: "Язык",
        theme: "Тема",
        close: "Закрыть",
        cancel: "Отмена",
        copy: "Копировать",
        copied: "Скопировано",
        clipboardUnavailable: "Буфер обмена недоступен",
        edit: "Изменить",
        delete: "Удалить",
        moveUp: "Переместить вверх",
        moveDown: "Переместить вниз",
        unableToLoadData: "Не удалось загрузить данные",
        loadErrorDescription: "Сейчас не получается загрузить данные. Попробуйте обновить страницу.",
        noneShort: "-",
        multiSelectList: {
          addItem: "Добавить элемент",
          emptyMessage: "Элементы не найдены.",
          availableItems: "Доступные элементы",
          noItemsSelected: "Элементы не выбраны",
          addFirstItem: "Добавьте первый элемент, чтобы начать формировать этот список.",
          removeItem: "Удалить {{item}}",
        },
      },
      language: {
        selectorAria: "Выбор языка",
        english: "Английский",
        russian: "Русский",
      },
      theme: {
        selectorAria: "Выбор темы",
        useSystem: "Как в системе",
        light: "Светлая",
        dark: "Тёмная",
      },
      nav: {
        groups: {
          general: "Общее",
          internet: "Интернет",
          networkRules: "Сетевые правила",
        },
        items: {
          systemMonitor: "Мониторинг системы",
          settings: "Настройки",
          outbounds: "Интерфейсы / выходы",
          dnsServers: "DNS-серверы",
          lists: "Списки",
          routingRules: "Правила маршрутизации",
          dnsRules: "DNS-правила",
        },
      },
      brand: {
        logoAlt: "логотип keen-pbr",
        tagline: "Пакет для пакетов с пакетами",
        openMenu: "Открыть меню",
      },
      warning: {
        compact: {
          draftPending: "Есть черновик конфигурации, ожидающий сохранения.",
          saving: "Сохранение...",
          apply: "Применить",
          resolverStale: "Конфиг dnsmasq устарел; требуется перезагрузка.",
          reloading: "Перезагрузка...",
          reload: "Перезагрузить",
        },
        full: {
          unsavedTitle: "Конфигурация не сохранена",
          unsavedDescription:
            "Конфигурация загружена в память. Сохраните и примените её, чтобы записать на диск и перезагрузить сервис.",
          applying: "Применение...",
          applyConfig: "Применить конфиг",
          staleTitle: "dnsmasq использует устаревший конфиг резолвера",
          staleDescription:
            "Ожидаемый хеш резолвера ({{expected}}…) не совпадает с активным хешем dnsmasq ({{actual}}…). Перезагрузите keen-pbr, чтобы пересобрать и применить текущую конфигурацию резолвера.",
          reloading: "Перезагрузка...",
          reloadService: "Перезагрузить сервис",
        },
      },
      overview: {
        service: {
          title: "keen-pbr",
          loadError: "Не удалось загрузить состояние сервиса.",
          unsupportedActionReason: "Недоступно в текущем API",
          version: "Версия",
          status: "Статус сервиса",
          dnsmasqGood: "dnsmasq в порядке",
          dnsmasqStale: "dnsmasq требуется перезапуск",
          actions: {
            start: "Запустить",
            stop: "Остановить",
            restart: "Перезапустить",
            reload: "Перезагрузить",
          },
        },
        outbounds: {
          title: "Состояние выходов",
          loadError: "Не удалось загрузить состояние выходов.",
          emptyTitle: "Выходы не настроены",
          emptyDescription: "Добавьте выходы, чтобы увидеть проверки состояния.",
          inUse: "Используется",
          urltestTitle: "urltest",
          headers: {
            tag: "Тег",
            destination: "Назначение",
            status: "Статус",
          },
          destination: {
            interface: "Интерфейс {{name}}",
            interfaceWithGateway: "Интерфейс {{name}} (шлюз: {{gateway}})",
            table: "Таблица {{value}}",
            outbound: "Выход {{name}}",
          },
        },
        routing: {
          title: "Состояние правил маршрутизации",
          loadError: "Не удалось загрузить проверки маршрутизации.",
          emptyTitle: "Проверки маршрутизации ещё не появились",
          emptyDescription: "Проверки маршрутизации появятся после следующей перезагрузки.",
        },
        dnsCheck: {
          card: {
            title: "Самопроверка DNS-сервера",
            description:
              "Подтверждает, что DNS-запросы доходят до встроенного тестового сервера из браузера и с другого устройства.",
            disabledDescription:
              "Включите `config.dns.dns_test_server`, чтобы запустить встроенную самопроверку DNS.",
            configuredServers: "Настроенные DNS-серверы",
            noServers: "На странице DNS-серверов не определено ни одного DNS-сервера.",
            via: "через {{detour}}",
            checking: "Проверка...",
            runAgain: "Запустить снова",
            testFromPc: "Проверить с ПК",
          },
          modal: {
            title: "Проверить DNS с другого устройства",
            description:
              "Запустите сгенерированную команду `nslookup` на ПК или телефоне, пока это окно остаётся открытым.",
            copyCommand: "Скопируйте и выполните эту команду:",
            warning:
              "Тестовый DNS-запрос ещё не поступил. Убедитесь, что устройство использует DNS вашего роутера, и попробуйте команду ещё раз.",
            copyAria: "Скопировать команду",
          },
          status: {
            disabled: "Встроенный DNS-пробник отключён в конфиге.",
            browserSuccess: "DNS-запрос из браузера достиг тестового сервера.",
            manualProbeSuccess: "Ручной DNS-запрос с устройства достиг тестового сервера.",
            browserProbeFail:
              "Запрос браузера завершился, но DNS-пробник не увидел lookup.",
            sseUnavailable:
              "Поток событий DNS в реальном времени недоступен, поэтому проверка не смогла запуститься.",
            browserFail: "Запрос браузера выполнился, но DNS lookup не был замечен.",
            sseFail: "Поток событий DNS в реальном времени не подключён.",
            browserChecking: "Проверяем DNS-путь браузера...",
            browserUnknown: "Статус DNS в браузере пока неизвестен.",
            manualSuccess: "Ручной тест устройства достиг DNS-пробника.",
            manualWaiting: "Ожидание вашей ручной команды nslookup...",
            manualIncomplete: "Ручной тест устройства ещё не завершён.",
          },
        },
      },
      pages: {
        routingRules: {
          title: "Правила маршрутизации",
          description: "Управляйте порядком правил маршрутизации и критериями сопоставления.",
          actions: { addRule: "Добавить правило" },
          messages: { saved: "Правила маршрутизации сохранены в черновик. Примените конфиг, чтобы записать их." },
          empty: {
            title: "Правил маршрутизации пока нет",
            description: "Добавьте правило маршрутизации, чтобы направлять подходящий трафик в outbound.",
          },
          headers: {
            order: "Порядок",
            criteria: "Критерии",
            outbound: "Outbound",
            actions: "Действия",
          },
          criteriaLabels: {
            lists: "Списки",
            proto: "Протокол",
            sourceIp: "Исходный IP",
            destinationIp: "IP назначения",
            sourcePort: "Исходный порт",
            destinationPort: "Порт назначения",
          },
        },
        routingRuleUpsert: {
          createTitle: "Создать правило маршрутизации",
          editTitle: "Изменить правило маршрутизации",
          description: "Правила маршрутизации проверяются по порядку и направляют трафик в настроенные outbounds.",
          cardDescription: "Выберите списки и outbound, затем при необходимости сузьте правило по протоколу, портам и адресам.",
          messages: { saved: "Правило маршрутизации сохранено в черновик. Примените конфиг, чтобы записать его." },
          missing: {
            cardDescription: "Запрошенное правило маршрутизации не найдено.",
            cardTitle: "Правило не найдено",
            description: "Вернитесь к таблице правил маршрутизации и выберите корректную запись.",
            back: "Назад к правилам маршрутизации",
          },
          validation: {
            selectList: "Выберите хотя бы один список.",
            outboundRequired: "Тег outbound обязателен.",
          },
          actions: { create: "Создать правило", save: "Сохранить правило" },
          fields: {
            lists: "Списки",
            listsPlaceholderDescription: "Добавьте один или несколько настроенных списков для этого правила.",
            noListsSelected: "Списки не выбраны",
            listsHint: "Названия списков берутся из ключей config.lists.",
            proto: "Протокол",
            any: "Любой",
            anyLower: "любой",
            protocol: "Протокол",
            protoHint: 'Оставьте proto пустым, чтобы использовать "любой".',
            sourcePort: "Исходный порт",
            destinationPort: "Порт назначения",
            portHint: "Порты или диапазоны через запятую. Для отрицания можно добавить ! в начале.",
            sourceAddresses: "Исходные адреса",
            destinationAddresses: "Адреса назначения",
            addressHint: "IP-адреса или CIDR через запятую. ! в начале инвертирует всё выражение.",
            outbound: "Outbound",
            selectOutbound: "Выберите outbound",
            configuredOutbounds: "Настроенные outbounds",
            outboundHint: "Теги outbound берутся из config.outbounds.",
          },
          placeholders: {
            sourcePort: "80,443 или 10000-20000",
            destinationPort: "443 или !53,123",
            sourceAddresses: "192.168.1.10,10.0.0.0/8",
            destinationAddresses: "2001:db8::1 или !203.0.113.0/24",
          },
        },
        outbounds: {
          title: "Интерфейсы / выходы",
          description: "Outbounds, на которые будет поступать трафик. Это может быть интерфейс, существующая таблица маршрутизации или авто-переключаемый интерфейс.",
          actions: { new: "Новый outbound" },
          empty: {
            title: "Outbounds пока нет",
            description: "Добавьте outbound, чтобы начать строить поведение маршрутизации.",
          },
          headers: {
            tag: "Тег",
            type: "Тип",
            summary: "Сводка",
            actions: "Действия",
          },
          summary: {
            interface: "ifname={{value}}",
            table: "table={{value}}",
            urltest: "outbounds={{value}}",
          },
          messages: {
            missingReference: 'Outbound "{{outbound}}" ссылается на отсутствующий тег "{{referenced}}".',
          },
        },
        outboundUpsert: {
          createTitle: "Создать outbound",
          editTitle: "Изменить outbound",
          editCardTitle: "Изменить {{tag}}",
          description: "Outbounds определяют прямые интерфейсы или сгруппированное поведение urltest.",
          cardDescription: "Настройте interface или urltest outbound.",
          missing: {
            cardDescription: "Запрошенный outbound не найден.",
            cardTitle: "Outbound не найден",
            description: "Вернитесь к таблице outbounds и выберите корректную запись.",
            back: "Назад к outbounds",
          },
          actions: { create: "Создать outbound", save: "Сохранить outbound" },
          common: { noExtraFields: "Для этого типа не нужны дополнительные поля, кроме тега outbound." },
          fields: {
            tag: "Тег",
            tagHint: "Используйте уникальный тег outbound, на который могут ссылаться правила, группы и detour.",
            type: "Тип",
            outboundTypes: "Типы outbound",
            typeHint: "Выберите тип outbound из схемы конфига; форма ниже покажет только подходящие поля.",
          },
          interface: {
            title: "Настройки интерфейса",
            description: "Настройте устройство выхода и необязательный шлюз для маршрутизации по интерфейсу.",
            interface: "Интерфейс",
            interfaceHint: "Имя сетевого интерфейса для выхода, например tun0 или eth0.",
            gateway: "Шлюз",
            gatewayHint: "Необязательный IP-адрес шлюза для этого interface outbound.",
          },
          table: {
            title: "Настройки таблицы",
            description: "Свяжите этот outbound с существующей таблицей маршрутизации ядра.",
            field: "ID таблицы",
            hint: "ID таблицы маршрутизации ядра, обязательный для типа table.",
          },
          blackhole: {
            title: "Поведение blackhole",
            description: "Outbounds типа blackhole намеренно отбрасывают весь подходящий трафик.",
          },
          ignore: {
            title: "Поведение ignore",
            description: "Outbounds типа ignore пропускают подходящий трафик без изменений policy-based routing.",
          },
          urltest: {
            groupsTitle: "Группы outbound",
            groupsDescription: "Группы пробуются по порядку. Каждая группа выбирает из interface outbounds, а порядок задаёт приоритет.",
            groupTitle: "Группа {{index}}",
            groupDescription: "Приоритет {{index}}. Более ранние группы предпочтительнее поздних.",
            interfaceOutbounds: "Interface outbounds",
            addOutbound: "Добавить outbound",
            noInterfaceOutbounds: "Interface outbounds не найдены.",
            addInterfaceOutboundsFirst: "Сначала добавьте interface outbounds, чтобы у групп urltest были цели для выбора.",
            addGroup: "Добавить группу",
            probingTitle: "Проверки и повторы",
            probingDescription: "Настройте, как группа urltest проверяет кандидатов и повторяет неудачные проверки.",
            probeUrl: "URL проверки",
            probeUrlHint: "Проверки доступности загружают этот URL, чтобы измерить доступность и задержку.",
            interval: "Интервал (мс)",
            intervalHint: "Интервал между проверками.",
            tolerance: "Допуск (мс)",
            toleranceHint: "Если разница задержки не превышает это значение, направление не меняется.",
            retryAttempts: "Число повторов",
            retryAttemptsHint: "Сколько дополнительных попыток выполнить перед тем, как считать проверку неуспешной.",
            retryInterval: "Интервал повтора (мс)",
            retryIntervalHint: "Задержка между повторными попытками после неудачной проверки.",
          },
          circuitBreaker: {
            title: "Circuit breaker",
            description: "Параметры резервного поведения при сбоях проверок.",
            failures: "Ошибок до открытия",
            failuresHint: "Открывать circuit после такого числа последовательных неудачных проверок.",
            successes: "Успехов до закрытия",
            successesHint: "Снова закрывать circuit после такого числа успешных восстановительных проверок.",
            timeout: "Таймаут открытия (мс)",
            timeoutHint: "Как долго circuit остаётся открытым перед переходом к half-open проверкам.",
            halfOpen: "Half-open проверки",
            halfOpenHint: "Максимум одновременных проверок при тестировании восстановления.",
          },
          strictEnforcement: {
            label: "Строгое применение",
            hint: "Переопределяет глобальную настройку strict routing для этого interface outbound.",
            default: "По умолчанию (как в глобальном конфиге)",
            enabled: "Включено",
            disabled: "Выключено",
          },
          validation: {
            duplicateTag: 'Тег outbound "{{tag}}" уже существует.',
            missingReference: 'Outbound "{{outbound}}" ссылается на отсутствующий тег "{{referenced}}".',
          },
        },
        dnsRules: {
          title: "DNS-правила",
          description: "Назначайте списки маршрутизации конкретным DNS-серверам.",
          actions: { add: "Добавить DNS-правило" },
          messages: { saved: "Конфигурация DNS сохранена в черновик. Примените конфиг, чтобы записать её." },
          validation: {
            invalidFallback: "Fallback-сервер должен ссылаться на существующий тег сервера.",
            invalidFallbackChange: "Нельзя изменить fallback, пока DNS-правила невалидны.",
            invalidResult: "Нельзя сохранить, потому что итоговые DNS-правила невалидны.",
          },
          fallback: {
            title: "Основной DNS-сервер",
            description: "Используется, когда ни одно DNS-правило не подходит текущему запросу.",
            field: "Тег DNS сервера по умолчанию",
            placeholder: "Выберите DNS-сервер",
            group: "DNS-серверы",
            noneDefined: "В config.dns.servers не определены DNS-серверы.",
          },
          empty: {
            title: "DNS-правил пока нет",
            description: "Добавьте DNS-правило, чтобы связывать настроенные списки с DNS-серверами.",
          },
          headers: {
            lists: "Списки",
            serverTag: "Тег сервера",
            actions: "Действия",
          },
        },
        dnsRuleUpsert: {
          createTitle: "Создать DNS-правило",
          editTitle: "Изменить DNS-правило",
          description: "DNS-правила связывают настроенные списки с тегами DNS-серверов.",
          cardDescription: "Укажите имена списков и DNS-сервер для этого правила.",
          messages: { saved: "DNS-правило сохранено в черновик. Примените конфиг, чтобы записать его." },
          validation: {
            notFound: "Запрошенное DNS-правило не найдено.",
            fixErrors: "Исправьте ошибки валидации перед сохранением.",
          },
          missing: {
            cardDescription: "Запрошенное DNS-правило не найдено.",
            cardTitle: "DNS-правило не найдено",
            description: "Вернитесь к DNS Rules и выберите корректную запись.",
            back: "Назад к DNS-правилам",
          },
          actions: { create: "Создать правило", save: "Сохранить правило" },
          fields: {
            serverTag: "Тег сервера",
            selectServer: "Выберите DNS-сервер",
            dnsServers: "DNS-серверы",
            noServers: "В config.dns.servers не определены DNS-серверы.",
            listNames: "Имена списков",
            listPlaceholderDescription: "Добавьте один или несколько настроенных списков для этого DNS-правила.",
            noListsSelected: "Списки не выбраны",
            noLists: "Не найдено ни одного списка. Пожалуйста, сначала создайте его на странице Списки.",
          },
        },
        lists: {
          title: "Списки",
          description:
            "Управляйте списками доменов и IP-адресов, которые используются в правилах маршрутизации и DNS.",
          actions: {
            new: "Новый список",
            update: "Обновить",
          },
          empty: {
            title: "Списков пока нет",
            description:
              "Создайте первый список, чтобы использовать его в правилах маршрутизации и DNS.",
          },
          headers: {
            name: "Имя",
            type: "Тип",
            stats: "Статистика",
            rules: "Правила",
            actions: "Действия",
          },
          delete: {
            confirm: 'Удалить список "{{name}}"?',
            confirmWithReferences:
              'Удалить список "{{name}}" и убрать его ссылки из правил маршрутизации и DNS?',
          },
          location: {
            inline: "Встроенный",
          },
          rule: {
            configured: "Настроен",
          },
          source: {
            url: "URL",
            file: "Файл",
            domains: "Домены",
            ip_cidrs: "IP CIDR",
            empty: "Пусто",
          },
        },
        listUpsert: {
          createTitle: "Создать список",
          editTitle: "Изменить список",
          editCardTitle: "Изменить {{name}}",
          fallbackName: "список",
          description:
            "Списки могут использовать файлы, встроенные источники или удалённые URL.",
          cardDescription:
            "Проверьте источник списка, TTL и содержимое перед сохранением.",
          messages: {
            created:
              "Список сохранён в черновик. Примените конфиг, чтобы записать его.",
            updated:
              "Изменения списка сохранены в черновик. Примените конфиг, чтобы записать их.",
          },
          missing: {
            cardDescription: "Запрошенный список не найден.",
            cardTitle: "Список не найден",
            description: "Вернитесь к таблице списков и выберите корректную запись.",
            back: "Назад к спискам",
          },
          actions: {
            saving: "Сохранение...",
            create: "Создать список",
            save: "Сохранить список",
          },
          fields: {
            name: "Имя",
            nameHint:
              "Используйте стабильный идентификатор, чтобы правила и ссылки на список оставались понятными.",
            ttlMs: "TTL мс",
            ttlMsHint:
              "Как долго IP-адреса, разрешённые из доменов этого списка, хранятся в IP set; 0 означает отсутствие таймаута.",
            url: "Удалённый URL",
            urlHint:
              "Необязательный удалённый источник по HTTP или HTTPS, который объединяется со списком.",
            file: "Локальный файл",
            fileHint:
              "Необязательный путь к локальному файлу. Его содержимое объединяется с inline-доменами, IP-адресами и данными из удалённого URL.",
            domains: "Домены",
            domainsHint:
              "Встроенные шаблоны доменов. Если указать example.com, автоматически будут включены и все поддомены.",
            ipCidrs: "IP CIDR",
            ipCidrsHint:
              "Встроенные IP-адреса или диапазоны CIDR, например 93.184.216.34, 10.0.0.0/8 или 2001:db8::/32.",
          },
          validation: {
            nameRequired: "Имя обязательно.",
            duplicateName: "Список с таким именем уже существует.",
            invalidTtl: "TTL должен быть неотрицательным целым числом.",
            sourceRequired:
              "Нужен хотя бы один источник: удалённый URL, локальный файл, домены или IP CIDR.",
          },
        },
      },
    },
  },
} as const

export function isLanguage(value: string | null): value is Language {
  if (value === null) {
    return false
  }

  return LANGUAGE_VALUES.includes(value as Language)
}

function detectInitialLanguage(): Language {
  if (typeof window !== "undefined") {
    const storedLanguage = window.localStorage.getItem(LANGUAGE_STORAGE_KEY)
    if (isLanguage(storedLanguage)) {
      return storedLanguage
    }
  }

  if (typeof navigator === "undefined") {
    return DEFAULT_LANGUAGE
  }

  const preferred = navigator.languages?.[0] ?? navigator.language
  return preferred.toLowerCase().startsWith("ru") ? "ru" : DEFAULT_LANGUAGE
}

void i18n.use(initReactI18next).init({
  resources,
  lng: detectInitialLanguage(),
  fallbackLng: DEFAULT_LANGUAGE,
  interpolation: { escapeValue: false },
})

export default i18n
