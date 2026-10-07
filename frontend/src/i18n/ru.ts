export const ruTranslation = {
  auth: {
    loading: "Загрузка keen-pbr…",
    title: "Вход в keen-pbr",
    description: "Введите пароль администратора, чтобы продолжить.",
    password: "Пароль",
    signIn: "Войти",
    signingIn: "Вход…",
    invalid: "Неверный пароль",
    failed: "Не удалось войти",
    signOut: "Выйти",
    warning: {
      prefix: "Аутентификация отключена. ",
      action: "Задайте пароль администратора",
      suffix: ", чтобы защитить API и веб-интерфейс.",
    },
    settings: {
      title: "Аутентификация и CORS",
      description:
        "Защитите веб-интерфейс и API паролем администратора. Новый вход в веб-интерфейс завершает предыдущую сессию.",
      enable: "Включить аутентификацию",
      newPassword: "Новый пароль",
      newPasswordPlaceholder: "Введите новый пароль",
      passwordSetPlaceholder: "Пароль установлен — введите новый для замены",
      confirmPassword: "Подтвердите пароль",
      allowedOrigins: "Разрешённые источники CORS",
      originsPlaceholder:
        "https://panel.example.com\nhttps://admin.example.net",
      originsDescription:
        "Указывайте по одному точному HTTP- или HTTPS-источнику в строке. Настройка нужна только если вы используете внешние UI для управления API keen-pbr.",
      passwordMismatch: "Пароли не совпадают.",
      passwordRequired: "Перед включением аутентификации задайте пароль.",
      updateFailed: "Не удалось обновить настройки аутентификации.",
      invalidOrigin:
        "Каждая запись CORS должна быть точным HTTP- или HTTPS-источником без пути, учётных данных, параметров запроса, фрагмента или шаблона.",
      saved: "Настройки аутентификации обновлены.",
    },
  },
  common: {
    documentation: "Документация",
    documentationUrl: "https://keen-pbr.fyi/ru/docs/",
    language: "Язык",
    theme: "Тема",
    enabled: "Включено",
    disabled: "Выключено",
    close: "Закрыть",
    cancel: "Отмена",
    copy: "Копировать",
    copied: "Скопировано",
    clipboardUnavailable: "Буфер обмена недоступен",
    edit: "Изменить",
    delete: "Удалить",
    moveUp: "Переместить вверх",
    moveDown: "Переместить вниз",
    dragToReorder: "Перетащить для изменения порядка",
    unableToLoadData: "Не удалось загрузить данные",
    loadErrorDescription:
      "Сейчас не получается загрузить данные. Попробуйте обновить страницу.",
    noneShort: "-",
    multiSelectList: {
      addItem: "Добавить элемент",
      emptyMessage: "Элементы не найдены.",
      availableItems: "Доступные элементы",
      noItemsSelected: "Элементы не выбраны",
      addFirstItem:
        "Добавьте первый элемент, чтобы начать формировать этот список.",
      removeItem: "Удалить {{item}}",
    },
    listUsage: {
      usedElsewhere: "Используется ещё в: {{summary}}",
    },
    interfacePicker: {
      open: "Открыть выбор интерфейса",
      empty: "Интерфейсы не найдены.",
      notExists: "(не существует)",
      notFound: "Интерфейс не существует.",
    },
    validation: {
      tagNamePattern:
        "Может содержать только a-z, 0-9 и подчёркивание. Максимум 24 символа, должен начинаться с буквы.",
    },
    selection: {
      selectAll: "Выбрать все видимые строки",
      selectRow: "Выбрать {{rowLabel}}",
    },
  },
  runtime: {
    healthy: "Исправен",
    notHealthy: "Неисправен",
    activeOutbound: "Активный outbound {{value}}",
    activeInterface: "Активный {{value}}",
    outboundStatus: {
      healthy: "Исправен",
      degraded: "Деградирован",
      unavailable: "Недоступен",
      unknown: "Неизвестно",
    },
    interfaceStatus: {
      active: "Активен",
      backup: "Резервный",
      degraded: "Деградирован",
      unavailable: "Недоступен",
      unknown: "Неизвестно",
    },
    statusTone: {
      healthy: "Исправен",
      degraded: "Деградирован",
      unknown: "Неизвестно",
    },
    fallback: {
      table: "Таблица маршрутизации {{value}}",
      blackhole: "Блокировать весь входящий трафик",
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
      networkRules: "Правила трафика",
    },
    items: {
      systemMonitor: "Обзор системы",
      requestsLog: "Журнал запросов",
      settings: "Настройки",
      security: "Безопасность",
      outbounds: "Outbounds (выходы)",
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
    draftChanged:
      "Конфигурация была изменена. Сохраните её на диск для применения.",
    actions: {
      applying: "Применение...",
      apply: "Применить",
      discarding: "Отмена изменений...",
      discard: "Отменить изменения",
      rollingBack: "Откат...",
      rollback: "Откатить",
      applyingAndRestarting: "Применение и перезапуск...",
      applyAndRestart: "Применить и перезапустить",
      restarting: "Перезапуск...",
      restart: "Перезапустить",
    },
    compact: {
      keenRestartRequired: "Несохранённые изменения",
      keenRestartRequiredDescription:
        "Настройки изменены. Примените их для перезапуска keen-pbr.",
      runtimeReloading: "Перезагрузка keen-pbr...",
      runtimeReloadingDescription: "Текущий этап: {{stage}}",
      runtimeReloadSucceeded: "keen-pbr готов",
      runtimeReloadSucceededDescription:
        "Маршрутизация использует ожидаемую конфигурацию.",
      runtimeApplying: "Применение конфигурации keen-pbr...",
      runtimeApplySucceeded: "Конфигурация keen-pbr применена",
      runtimeApplyFailed: "Не удалось применить конфигурацию keen-pbr",
      runtimeRollingBack: "Откат конфигурации keen-pbr...",
      runtimeRollbackSucceeded: "Предыдущая конфигурация восстановлена",
      runtimeRollbackFailed: "Не удалось откатить конфигурацию",
      runtimeStarting: "Запуск keen-pbr...",
      runtimeStartSucceeded: "keen-pbr запущен",
      runtimeStartFailed: "Не удалось запустить keen-pbr",
      runtimeStopping: "Остановка keen-pbr...",
      runtimeStopSucceeded: "keen-pbr остановлен",
      runtimeStopFailed: "Не удалось остановить keen-pbr",
      runtimeStartingDescription:
        "Маршрутизация и межсетевой экран запускаются. Подождите.",
      runtimeReloadFailed: "Не удалось перезагрузить keen-pbr",
      runtimeReloadFailedDescription:
        "Не удалось завершить перезагрузку маршрутизации. Попробуйте применить настройки и перезапустить ещё раз.",
    },
    full: {
      unsavedTitle: "Конфигурация не сохранена",
    },
  },
  lifecycle: {
    stages: {
      validate_config: "Проверка конфигурации",
      prepare_remote_lists: "Подготовка удалённых списков",
      reconcile_runtime: "Согласование маршрутизации и межсетевого экрана",
      stop_routing: "Остановка маршрутизации и межсетевого экрана",
      start_routing: "Запуск маршрутизации и межсетевого экрана",
      commit_config: "Сохранение конфигурации",
      restore_config: "Восстановление предыдущей конфигурации",
    },
  },
  overview: {
    status: {
      ok: "Всё работает",
      degraded: "Маршрутизация работает с ограничениями",
      stopped: "Служба остановлена",
      issuesPrefix: "Система работает, но есть",
      issueCount_one: "{{count}} проблема",
      issueCount_few: "{{count}} проблемы",
      issueCount_many: "{{count}} проблем",
      issueCount_other: "{{count}} проблемы",
      versionLine: "keen-pbr {{version}} · сборка {{build}} · {{os}}",
      confirmStop: "Остановить службу?",
      confirmStopAction: "Да, остановить",
      busy: {
        start: "Служба запускается…",
        stop: "Служба останавливается…",
        restart: "Служба перезапускается…",
        apply: "Применяется конфигурация…",
      },
      busyAction: {
        start: "Запуск…",
        stop: "Остановка…",
        restart: "Перезапуск…",
        apply: "Применение…",
      },
    },
    diagnostics: {
      allChecks: "все проверки",
      onlyProblems: "только проблемы",
      showHealthy: "Показать исправные",
      noIssues: "Проблем нет",
      open: "Открыть",
    },
    issues: {
      capabilityUnsupported: "Не поддерживается ядром",
      capabilityFallback:
        "Недоступно в ядре; вместо этого используются обычные правила по адресам",
      kernelCheckFailed: "Проверка ядра не пройдена: {{reason}}",
      interceptLimited: "Перехват трафика ограничен",
      interceptWarning: "Предупреждение перехвата трафика",
      routingCheckFailed: "Не удалось выполнить проверки маршрутизации",
      chainMissing: "Цепочка файрвола отсутствует",
      preroutingMissing: "Цепочка файрвола не подключена к PREROUTING",
      firewallRules: "Правила файрвола не совпадают: {{count}}",
      routes: "Таблицы маршрутизации не совпадают: {{count}}",
      policies: "Политики маршрутизации не совпадают: {{count}}",
      dnsmasqError: "dnsmasq не применил DNS-правила",
      dnsmasqDead: "dnsmasq не запущен",
      dnsmasqReconciling: "DNS-правила восстанавливаются",
      outboundUnavailable: "{{tag}} недоступен",
      outboundDegraded: "{{tag}} работает с перебоями",
      groupMembersFailing: "{{tag}}: неисправных участников: {{count}}",
    },
    healthy: {
      firewallOk: "цепочка и хук PREROUTING на месте",
      firewallPartial: "цепочка неполная",
      kernelWithRelease: "Проверки ядра ({{release}})",
      passed: "{{passed}} из {{total}} пройдено",
    },
    counters: {
      title: "Счётчики",
      collapse: "Свернуть",
      showAll: "Все {{count}}",
      short: {
        dnsPackets: "DNS",
        dnsMatched: "DNS совп.",
        l7Packets: "L7",
        l7Matched: "L7 совп.",
      },
      groups: {
        dns: "DNS",
        l7: "L7 и маркер",
        sets: "Наборы",
        conntrack: "Conntrack",
        queue: "Очередь",
      },
    },
    pageDescription:
      "Обзор состояния маршрутизации, конфигурации и активных outbounds",
    runtime: {
      title: "Маршрутизация",
      loadError: "Не удалось загрузить состояние маршрутизации.",
      version: "Версия",
      build: "Сборка",
      router: "Роутер",
      routingStatus: "Статус маршрутизации",
      actions: {
        start: "Запустить",
        stop: "Остановить",
        restart: "Перезапустить",
      },
    },
    outbounds: {
      title: "Outbounds",
      summary: "{{configured}} настроено · групп: {{groups}}",
      online: "{{count}} online",
      manage: "Управлять",
      plainTitle: "Простые outbounds",
      latency: "{{value}} мс",
      packetsTitle: "Ответов на пробы: {{received}} из {{attempted}}",
      columns: {
        group: "Группа",
        strategy: "Стратегия",
        members: "Участники",
      },
      loadError: "Не удалось загрузить состояние outbounds.",
      emptyTitle: "Outbounds не настроены",
      emptyDescription: "Добавьте outbounds, чтобы увидеть проверки состояния.",
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
        outbound: "Outbound {{name}}",
      },
    },
    routing: {
      title: "Диагностика",
      loadError: "Не удалось загрузить проверки маршрутизации.",
      emptyTitle: "Проверки маршрутизации ещё не появились",
      emptyDescription:
        "Проверки маршрутизации появятся после следующего применения или перезапуска маршрутизации.",
      showHealthyEntries: "Показать и здоровые записи",
      allHealthyTitle: "Всё в порядке",
      allHealthyDescription:
        "Сейчас нет проблемных записей в диагностике маршрутизации.",
      noChecksTitle: "Проверок нет",
      noChecksDescription:
        "Для диагностики маршрутизации нет записей для отображения.",
      sections: {
        firewall: "Firewall",
        routes: "Маршруты",
        policies: "Политики",
      },
      chain: "chain",
      prerouting: "prerouting",
      kernel: "ядро",
      defaultRoute: "default",
      ipv4: "IPv4",
      ipv6: "IPv6",
      yes: "да",
      no: "нет",
      tableLabel: "таблица {{value}}",
      priorityLabel: "приоритет {{value}}",
      fwmarkLabel: "fwmark {{value}}",
      fwmarkExpectedActual: "ожидалось {{expected}}, получено {{actual}}",
      actualLabel: "фактически {{value}}",
      routeTypeFallback: "маршрут",
      routeVia: "через {{value}}",
      routeGateway: "шлюз {{value}}",
      routeMetric: "метрика {{value}}",
      issues: {
        tableMissing: "таблица отсутствует",
        defaultRouteMissing: "маршрут по умолчанию отсутствует",
        interfaceMismatch: "несовпадение интерфейса",
        gatewayMismatch: "несовпадение шлюза",
      },
    },
    diagnosticsDownload: {
      button: "Скачать файл диагностики",
      modal: {
        title: "Внимание, чувствительные данные!",
        description: "Файл диагностики содержит следующие данные:",
        items: {
          config:
            "Ваш конфигурационный файл целиком (включая используемые списки)",
          serviceHealth: "Состояние сервиса",
          routingHealth: "Состояние маршрутизации",
          outbounds: "Состояние outbounds",
          names: "Наименования списков, outbounds, интерфейсов",
        },
        trustWarning:
          "Пожалуйста, передавайте данный файл только тому, кому вы доверяете.",
        hideListsOption: "Скрыть содержимое списков и URL-адреса на списки",
        downloadAction: "Скачать файл диагностики",
      },
    },
    dnsCheck: {
      card: {
        title: "Перехват DNS",
        description:
          "Наблюдает DNS-трафик через keen-pbr из этого браузера или с другого устройства; замена синтетического ответа отдельно не проверяется.",
        disabledDescription:
          "Включите перехват DNS в настройках, чтобы запустить самопроверку DNS.",
        runtimeDisabledDescription:
          "Перехват DNS недоступен, поэтому самопроверка по домену-маркеру отключена.",
        configuredServers: "Настроенные DNS-серверы",
        noServers:
          "На странице DNS-серверов не определено ни одного DNS-сервера.",
        via: "через {{detour}}",
        checking: "Проверка...",
        checkAgain: "Проверить снова",
        testFromPc: "Проверить с другого устройства",
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
        disabled: "Перехват DNS отключён в конфигурации.",
        runtimeDisabled: "Перехват не работает: удержание DNS не активно",
        browserSuccess:
          "Перехват DNS-запросов из этого браузера работает",
        manualProbeSuccess: "Перехватчик увидел DNS-запрос от устройства.",
        browserProbeFail:
          "Запрос браузера завершился, но перехватчик не увидел lookup маркера.",
        sseUnavailable:
          "Поток событий DNS в реальном времени недоступен, поэтому проверка не смогла запуститься.",
        browserFail:
          "Запрос браузера выполнился, но перехватчик не увидел lookup маркера.",
        sseFail: "Поток событий DNS в реальном времени не подключён.",
        sseStalled:
          "Браузер не смог открыть поток событий DNS в реальном времени. Возможно, открыто слишком много вкладок keen-pbr (браузеры разрешают 6 соединений на сайт) — закройте лишние вкладки keen-pbr и повторите.",
        sseHttp:
          "Запрос потока событий DNS в реальном времени завершился ошибкой (HTTP {{status}}).",
        browserChecking: "Проверяем DNS-путь браузера...",
        browserUnknown: "Статус DNS в браузере пока неизвестен.",
        manualSuccess: "Перехватчик увидел DNS-запрос от устройства.",
        manualWaiting: "Ожидание вашей ручной команды nslookup...",
        manualIncomplete: "Ручной тест устройства ещё не завершён.",
      },
    },
    dnsRules: {
      server: "DNS-сервер",
      inactive: "Не активно",
      rulesAndDomains: "Правила / домены",
      title: "Правила DNS",
      state: {
        ok: "Синхронизирован",
        applying: "Применяется",
        reconciling: "Восстановление",
        error: "Ошибка",
        disabled: "Отключён",
      },
      rules: "DNS-правила",
      domains: "Домены",
      lastSync: "Последняя синхронизация",
      neverSynced: "Ещё не синхронизировано",
      loadedAt: "dnsmasq загрузил конфигурацию",
      externalReload: "dnsmasq перезапущен вне keen-pbr",
      disabledDescription: "Интеграция правил DNS отключена.",
      alive: {
        label: "Служба dnsmasq",
        dead: "не запущен",
        unknown: "неизвестно",
      },
      repairScheduled: "Попытка восстановления {{n}}/{{max}} в {{time}}.",
      repairRestarting: "Перезапуск dnsmasq (попытка {{n}}/{{max}}).",
      repairPaused: "Автоматическое восстановление остановлено после {{max}} попыток.",
      repairPausedHint: "Кнопка «Применить» или «Перезапустить» включит его снова.",
      lastError: "Последняя ошибка",
    },
    intercept: {
      title: "Перехват трафика",
      rows: {
        dns: {
          title: "Перехват DNS",
          description: "определение доменов по ответам",
        },
        dnsHold: {
          title: "Удержание DNS",
          description: "ответ ждёт заполнения наборов",
        },
        l7: {
          title: "Перехват L7",
          description: "TLS SNI · HTTP Host · QUIC",
        },
      },
      description: "Состояние перехвата DNS и L7 на стороне демона.",
      status: {
        disabled: "Отключён",
        running: "Работает",
        stopped: "Не работает",
      },
      dnsHoldActive: "Удержание DNS включено",
      dnsHoldInactive: "Удержание DNS выключено",
      l7Active: "L7 включён",
      l7Inactive: "L7 выключен",
      summary: {
        processor: "Обработчик DNS/DPI",
        dnsHold: "Удержание DNS",
        dpi: "DPI (SNI/Host)",
        enabled: "Включено",
        disabled: "Отключено",
      },
      checksTitle: "Проверки ядра",
      countersTitle: "Счётчики перехвата",
      moreCounters: "Показать подробные счётчики",
      supported: "доступно",
      unsupported: "недоступно",
      unsupportedWarning: "Некоторые функции перехвата недоступны",
      diagnosticErrors: "Проверки ядра сообщили об ошибках",
      capabilities: {
        nfqueue: "NFQUEUE",
        nflog: "NFLOG",
        connbytes: "connbytes",
        addrtype: "addrtype",
      },
      probes: {
        title: "Проверки ядра",
        kernel: "Ядро {{release}}",
        status: {
          ok: "ок",
          unsupported: "не поддерживается",
          error: "ошибка",
          skipped: "пропущено",
          not_run: "не выполнялась",
        },
      },
      kernelQueue: {
        queueTotal: "В очереди",
        queueDropped: "Отброшено ядром",
        userDropped: "Отброшено пользователем",
        idSequence: "Счётчик ядра",
      },
      counters: {
        dnsPackets: "DNS-пакеты",
        dnsParseErrors: "Ошибки разбора DNS",
        dnsMatched: "Совпадения DNS",
        dnsHoldTimeouts: "Тайм-ауты удержания DNS",
        dnsTcpPartial: "Неполные DNS TCP",
        markerHits: "Попадания маркера",
        l7Packets: "L7-пакеты",
        l7Matched: "Совпадения L7",
        setAdded: "Добавлено в наборы",
        setRefreshed: "Обновлено в наборах",
        setErrors: "Ошибки наборов",
        conntrackRequests: "Запросы conntrack",
        conntrackDeleted: "Удалено conntrack",
        conntrackErrors: "Ошибки conntrack",
        queueOverruns: "Переполнения очереди",
        logOverruns: "Переполнения журнала",
      },
    },
    routingTest: {
      title: "Куда пойдёт трафик?",
      description: "Проверка домена или IP-адреса",
      placeholder: "напр. google.com или 1.2.3.4",
      submit: "Проверить маршрут",
      invalidTarget: "Введите корректный домен или IP-адрес.",
      requestFailed: "Проверка маршрута не удалась. Попробуйте ещё раз.",
      emptyTitle: "Маршрут не найден",
      emptyDescription: "Попробуйте другой домен или IP-адрес.",
    },
    routingDiagnostics: {
      noMatchingRule:
        "Для целевых списков не найдено подходящего правила маршрутизации.",
      resultTitle: "Результат маршрутизации",
      ruleDetailsTitle: "Диагностика правил",
      ip: "IP",
      resultListMatch: "Совпадение со списком",
      resultListMatchVia: "{{list}} (через {{via}})",
      expectedOutbound: "Ожидаемый outbound",
      actualOutbound: "Фактический outbound",
      status: "Статус",
      hostLabel: 'Хост "{{target}}"',
      inRuleLists: "Есть в доменных/IP-списках правила?",
      showAllRules: "Показывать все правила",
      listMatch: "{{list}}: {{via}}",
      noConditions: "Без дополнительных условий",
      conditions: {
        lists: "Списки",
        proto: "Протокол",
        sourceIp: "IP источника",
        destinationIp: "IP назначения",
        sourcePort: "Порт источника",
        destinationPort: "Порт назначения",
      },
    },
    routingLegend: {
      title: "Условные обозначения",
      inLists: "Есть в доменных/IP-списках",
      notInLists: "Нет в доменных/IP-списках",
      inIpsetAndLists: "Есть в IPSet и в списках",
      notInIpsetAndNotInLists: "Нет в IPSet и нет в списках",
      inIpsetButShouldNotBe: "Есть в IPSet, хотя не должно быть",
      notInIpsetButShouldBe: "Нет в IPSet, хотя должно быть",
    },
  },
  requestsLog: {
    empty: "Запросов пока нет.",
    gap: "События {{from}}–{{to}} потеряны до доставки.",
    copyIps: "Скопировать IP-адреса: {{value}}",
    filters: {
      count: "{{shown}} из {{total}}",
      clear: "Сбросить фильтры",
      noMatches: "Нет запросов, подходящих под фильтры.",
      invalidIp: "Некорректная CIDR-подсеть; фильтр по IP игнорируется.",
      hideEmptyAnswers: "Скрыть пустые ответы",
      placeholder: {
        device: "192.168.1.*",
        domain: "*.example.com",
        ip: "10.0.0.0/8, 192.168.*",
      },
      hint: {
        device:
          "Шаблоны: * любой текст, ? один символ. Без шаблонов ищет подстроку.",
        domain:
          "Шаблоны: * любой текст, ? один символ. Без шаблонов ищет подстроку.",
        ip: "Точный IP, шаблон (10.0.*, *::1) или CIDR-подсеть (10.0.0.0/8, 2001:db8::/32). Подходит любой из полученных адресов.",
      },
    },
    columns: {
      device: "Устройство",
      method: "Метод",
      domain: "Домен",
      lists: "Списки",
      ip: "IP",
      processingTime: "Время обработки",
      flags: "Флаги",
    },
    timeout: {
      budget_spent_by_batch: {
        label: "поздно: очередь",
        tooltip:
          "Предыдущие пакеты того же пробуждения израсходовали общий бюджет удержания, поэтому ответ был отпущен до записи в set.",
      },
      admission_blocked: {
        label: "поздно: применение",
        tooltip:
          "Дедлайн удержания истёк, пока запись в set ждала завершения применения firewall.",
      },
      own_write_slow: {
        label: "поздно: запись",
        tooltip: "Сама запись в set заняла больше времени, чем дедлайн удержания.",
      },
      late_batch_full: {
        label: "отброшено: очередь полна",
        tooltip:
          "Очередь отложенных записей была переполнена, поэтому эти элементы set были отброшены.",
      },
      other: {
        label: "поздно: другое",
        tooltip: "Дедлайн удержания истёк по другой причине.",
      },
    },
    methods: {
      dns: "DNS",
      http: "HTTP Host",
      sni: "HTTPS SNI",
      quic: "QUIC",
      marker: "Маркер DNS",
    },
    methodTooltips: {
      dns: "DNS-ответ",
      http: "HTTP-запрос (заголовок Host)",
      sni: "HTTPS-соединение (TLS SNI)",
      quic: "QUIC-соединение (Initial SNI)",
      marker: "Маркер проверки DNS",
    },
    decimalSeparator: ",",
    units: { us: "µs", ms: "мс", s: "с" },
    dnsReasons: {
      nxdomain: "NXDOMAIN",
      servfail: "SERVFAIL",
      refused: "REFUSED",
      rcode: "RCODE {{code}}",
      nodata: "нет {{type}}",
      nodataOther: "запись {{type}}",
    },
    dnsTooltips: {
      nxdomain: "Доменное имя не существует (NXDOMAIN)",
      servfail: "Ошибка сервера (SERVFAIL)",
      refused: "Запрос отклонен (REFUSED)",
      rcode: "Код ошибки DNS {{code}}",
      nodata: "DNS-сервер ответил NOERROR без записей {{type}} (NODATA)",
      nodataOther: "DNS-сервер ответил NOERROR без записей {{type}} (NODATA)",
    },
    flags: {
      added_one: "Добавлен {{count}} новый адрес в набор маршрутизации",
      added_few: "Добавлено {{count}} новых адреса в набор маршрутизации",
      added_many: "Добавлено {{count}} новых адресов в набор маршрутизации",
      added_other: "Добавлено {{count}} новых адресов в набор маршрутизации",
      refreshed_one: "{{count}} адрес уже был в наборе, его срок продлён",
      refreshed_few: "{{count}} адреса уже были в наборе, их срок продлён",
      refreshed_many: "{{count}} адресов уже были в наборе, их срок продлён",
      refreshed_other: "{{count}} адресов уже были в наборе, их срок продлён",
      errors_one: "{{count}} адрес не удалось записать в набор",
      errors_few: "{{count}} адреса не удалось записать в набор",
      errors_many: "{{count}} адресов не удалось записать в набор",
      errors_other: "{{count}} адресов не удалось записать в набор",
      not_learned_one: "{{count}} блокирующий или немаршрутизируемый адрес не изучен",
      not_learned_few: "{{count}} блокирующих или немаршрутизируемых адреса не изучены",
      not_learned_many: "{{count}} блокирующих или немаршрутизируемых адресов не изучены",
      not_learned_other: "{{count}} блокирующих или немаршрутизируемых адресов не изучены",
      not_learned_tooltip:
        "Блокирующий или немаршрутизируемый ответ (0.0.0.0, ::, loopback) — не добавлен в списки",
      seq: "Запрос №{{seq}} (порядковый номер события)",
      time: "Замечен в {{time}} ({{date}})",
      parse: "Ответ разобран за {{value}}",
      setWrite: "Запись в набор заняла {{value}}",
    },
  },
  pages: {
    security: {
      title: "Безопасность",
      description: "Управляйте доступом к веб-интерфейсу и API.",
    },
    settings: {
      title: "Настройки",
      description:
        "Глобальные настройки, действующие на все outbounds и правила.",
      saved:
        "Настройки сохранены в черновик. Примените новый конфиг, чтобы записать их.",
      webUi: {
        title: "Веб-интерфейс",
        description: "Настройте отображаемое имя этой установки keen-pbr.",
      },
      general: {
        title: "Общие",
        description: "Поведение по умолчанию для всех outbounds.",
        deviceNameLabel: "Имя устройства",
        deviceNamePlaceholder: "Например, Домашний роутер",
        deviceNameHint:
          "Отображается в заголовке страницы браузера и под логотипом keen-pbr. Оставьте поле пустым, чтобы использовать стандартное оформление.",
        strictEnforcementLabel:
          "Блокировать трафик при падении outbound (kill-switch)",
        strictEnforcementHint:
          "Если VPN или интерфейс отключится, трафик по его правилам будет заблокирован, а не отправлен через основную таблицу маршрутизации. Можно переопределить для каждого outbound.",
        skipMarkedPacketsLabel: "Не обрабатывать маркированные пакеты",
        skipMarkedPacketsHint:
          "Игнорировать пакеты, у которых уже есть fwmark проставленный другими правилами firewall, чтобы policy routing не обрабатывал их повторно.",
        processRouterTrafficLabel: "Обрабатывать собственный трафик роутера",
        processRouterTrafficHint:
          "Применять правила маршрутизации и изучать домены для трафика, который генерирует сам роутер. Если отключено, правилами маршрутизируется только транзитный трафик LAN; DNS detour по-прежнему применяется к собственным DNS-запросам роутера.",
        clearDynamicSetsOnApplyLabel:
          "Очищать изученные адреса доменов при применении",
        clearDynamicSetsOnApplyHint:
          "Очищать динамические наборы firewall, заполняемые по DNS-ответам, при полном применении конфигурации или перезапуске маршрутизации. Отключите, чтобы сохранять адреса до истечения их TTL.",
        ipv6EnabledLabel: "Включить поддержку IPv6",
        ipv6EnabledHint:
          "Создавать IPv6-наборы firewall и изучать IPv6-назначения. Отключите на старых прошивках без поддержки IPv6 netfilter.",
        inboundInterfacesLabel: "Входящие интерфейсы",
        inboundInterfacesHint:
          "Policy routing будет применяться только к пакетам, пришедшим через выбранные интерфейсы. Оставьте поле пустым, чтобы обрабатывать трафик с любых интерфейсов.",
        inboundInterfacesAddAction: "Добавить интерфейс",
        inboundInterfacesLoading: "Загрузка интерфейсов...",
        inboundInterfacesNoAvailable:
          "Больше нет доступных интерфейсов для добавления.",
        inboundInterfacesEmptyTitle: "Входящие интерфейсы не выбраны",
        inboundInterfacesEmptyDescription:
          "Добавьте интерфейсы, если policy routing должен применяться только к определённым входящим интерфейсам.",
        inboundInterfacesLoadError:
          "Живая инвентаризация интерфейсов временно недоступна. Сохранённые значения всё равно можно редактировать.",
        inboundInterfacesStatusUp: "UP",
        inboundInterfacesStatusDown: "DOWN",
        inboundInterfacesStatusLoading: "Загрузка",
        inboundInterfacesStatusMissing: "Отсутствует",
        inboundInterfacesMissingDetail:
          "Этот интерфейс сохранён в конфиге, но сейчас отсутствует в живом списке интерфейсов системы.",
      },
      intercept: {
        title: "Перехват трафика",
        description:
          "Заполняйте динамические наборы из DNS-ответов и TLS, HTTP или QUIC-трафика.",
        enabledLabel: "Включить перехват трафика",
        enabledHint:
          "При включении keen-pbr узнаёт назначения.",
        minTtlLabel: "Минимальный TTL (секунды)",
        minTtlHint: "Минимальное время элементов в наборах.",
        maxTtlLabel: "Максимальный TTL (секунды)",
        maxTtlHint: "Максимальное время элементов в наборах.",
        dnsEnabledLabel: "Включить перехват DNS",
        dnsEnabledHint:
          "Ненадолго удерживать DNS-ответы, пока адреса добавляются в наборы.",
        queueLabel: "Номер NFQUEUE",
        queueHint: "Очередь для перехвата DNS-ответов.",
        holdTimeoutLabel: "Тайм-аут удержания DNS (миллисекунды)",
        holdTimeoutHint: "Максимальное удержание DNS-ответа (5–500 мс).",
        markerDomainLabel: "Домен-маркер",
        markerDomainHint: "Домен для синтетического DNS-ответа.",
        markerAddressLabel: "IPv4-адрес маркера",
        markerAddressHint: "IPv4-адрес, возвращаемый для домена-маркера.",
        l7EnabledLabel: "Включить перехват L7",
        l7EnabledHint: "Узнавать назначения из TLS SNI, HTTP Host и QUIC.",
        nflogGroupLabel: "Группа NFLOG",
        nflogGroupHint: "Группа для событий TLS, HTTP и QUIC.",
        tlsLabel: "TLS SNI",
        httpLabel: "HTTP Host",
        quicLabel: "QUIC",
      },
      autoupdate: {
        title: "Автообновление списков",
        description: "Автоматическое обновление удалённых списков.",
        enabledLabel: "Включить автообновление списков",
        enabledHint:
          "Автоматически скачивать обновления удалённых списков и обновлять маршрутизацию при изменениях.",
        cronLabel: "Расписание обновления",
        cronHintPrefix:
          "Как часто проверять обновления. Формат cron. Используйте",
        cronHintSuffix: "для помощи.",
        openInGuru: "Открыть в Crontab Guru",
      },
      advanced: {
        title: "Расширенные настройки маршрутизации",
        description:
          "Расширенные настройки - меняйте только если понимаете, что делаете.",
        fwmarkStartLabel: "Начальное значение firewall mark",
        fwmarkStartHint:
          "Начальное значение fwmark для первого outbound. Каждый следующий outbound получает следующее значение в диапазоне.",
        fwmarkMaskLabel: "Маска firewall mark",
        fwmarkMaskHintPrefix:
          "Битовая маска, определяющая, какие биты используются для fwmark. Должна содержать непрерывный блок hex-цифр",
        fwmarkMaskHintSuffix: "например",
        tableStartLabel: "Начальное значение таблицы маршрутизации IP",
        tableStartHint:
          "ID таблицы маршрутизации для первого outbound. Каждый следующий outbound получает следующий ID.",
        ipsetHashsizeLabel: "Размер хеш-таблицы IPSet",
        ipsetHashsizeHint:
          "Необязательная настройка только для iptables для каждого набора hash:net. Оставьте пустым для значения ipset по умолчанию (1024); с nftables не действует. Изменение при работающем iptables пересоздаёт owned ipset и очищает изученные адреса.",
        ipsetMaxelemLabel: "Максимум элементов IPSet",
        ipsetMaxelemHint:
          "Необязательная настройка только для iptables для каждого набора hash:net. Оставьте пустым для значения ipset по умолчанию (65536); с nftables не действует. Изменение при работающем iptables пересоздаёт owned ipset и очищает изученные адреса.",
      },
      actions: {
        saving: "Сохранение...",
        save: "Сохранить",
      },
    },
    dnsServers: {
      title: "DNS-серверы",
      description: "Upstream DNS-серверы для разрешения доменных имён.",
      keeneticAddress: "Встроенный DNS Keenetic",
      actions: {
        add: "Добавить DNS-сервер",
      },
      empty: {
        title: "DNS-серверов пока нет",
        description:
          "Добавьте DNS-сервер, чтобы настроить upstream-разрешение.",
      },
      loadErrorDescription:
        "Сейчас не получается загрузить DNS-серверы. Попробуйте обновить страницу.",
      headers: {
        name: "Название",
        address: "Адрес",
        outbound: "Outbound",
        actions: "Действия",
      },
      delete: {
        confirmWithReferences:
          'DNS-сервер "{{serverTag}}" сейчас используется в {{count}} правил(е/ах){{fallbackSuffix}}.\nУдалить и автоматически убрать эти ссылки?',
        fallbackSuffix: " и как fallback",
      },
      deleteDialog: {
        title: "Удалить DNS-серверы?",
        description:
          "При подтверждении операции будут произведены следующие действия:",
        confirm: "Удалить",
        items: {
          serverPrefix: "DNS-сервер",
          serverSuffix: "будет удалён.",
          dnsRule: "DNS-правило #{{number}} будет удалено.",
          fallback: "Fallback DNS будет изменён.",
        },
      },
      bulk: {
        selected: "Выбрано: {{count}}",
        delete: "Удалить выбранные",
        confirmDelete:
          "Удалить DNS-серверы: {{tags}}?\nАвтоматически убрать ссылки из правил?",
      },
      none: "нет",
    },
    dnsServerUpsert: {
      createTitle: "Создать DNS-сервер",
      editTitle: "Изменить DNS-сервер",
      missingCardDescription: "Запрошенный DNS-сервер не найден.",
      missingCardTitle: "DNS-сервер не найден",
      missingDescription:
        "Вернитесь к таблице DNS-серверов и выберите корректную запись.",
      back: "Назад к DNS-серверам",
      description: "Этот сервер будет доступен в DNS-правилах и как fallback.",
      cardDescription:
        "Выберите тип DNS-сервера и необязательный detour outbound.",
      editCardTitle: "Изменить {{tag}}",
      fields: {
        tag: "Название",
        tagHint: "Короткое название сервера для использования в DNS-правилах.",
        type: "Тип DNS",
        typeHint:
          "Keenetic использует текущий встроенный DNS роутера. Plaintext DNS использует IP-адрес, введённый вручную.",
        typeOptions: {
          keenetic: "Keenetic DNS",
          static: "Plaintext DNS",
        },
        keeneticNotice: {
          description:
            "Для этого режима DNS-серверы нужно настроить в веб-интерфейсе Keenetic.",
          openLink: "Перейти к настройке",
          navigation:
            "Перейдите в Сетевые правила -> Интернет-фильтры -> Настройка DNS.",
          dotDohOnly:
            "Если там настроены DoT или DoH серверы, будут использоваться только они.",
        },
        address: "Адрес",
        addressPlaceholder: "1.1.1.1 или [2606:4700::1111]:53",
        addressHint:
          "IP-адрес сервера, напр. `1.1.1.1` или `[2606:4700::1111]:53`.",
        detour: "Делать запросы через Outbound",
        detourEmpty: "Не выбрано",
        detourPlaceholder: "Необязательный тег outbound",
        detourHint:
          "Необязательно: отправлять DNS-запросы к этому серверу через конкретный outbound (например, VPN).",
      },
      validation: {
        tagRequired: "Название обязательно.",
        tagUnique: "Название должно быть уникальным.",
        typeRequired: "Тип DNS обязателен.",
        addressRequired: "Адрес обязателен.",
        addressInvalid:
          "Адрес должен быть корректным IPv4/IPv6 значением с необязательным портом.",
      },
      actions: {
        create: "Создать DNS-сервер",
        save: "Сохранить DNS-сервер",
      },
    },
    routingRules: {
      title: "Правила маршрутизации",
      description:
        "Правила, определяющие, какой outbound обрабатывает подходящий трафик. Проверяются сверху вниз.",
      actions: {
        addRule: "Добавить правило маршрутизации",
        enableRule: "Включить правило",
        disableRule: "Выключить правило",
      },
      messages: {
        saved:
          "Правила маршрутизации сохранены в черновик. Примените новый конфиг, чтобы записать их.",
      },
      bulk: {
        selected: "Выбрано: {{count}}",
        enable: "Включить выбранные",
        disable: "Выключить выбранные",
        delete: "Удалить выбранные",
        confirmDelete:
          "Удалить {{count}} правил(о/а) маршрутизации? Изменение нельзя отменить здесь одним действием.",
      },
      empty: {
        title: "Правил маршрутизации пока нет",
        description:
          "Добавьте правило маршрутизации, чтобы направлять подходящий трафик в outbound.",
      },
      headers: {
        order: "Порядок",
        criteria: "Условие",
        outbound: "Outbound",
        runtime: "Состояние",
        actions: "Действия",
      },
      criteriaLabels: {
        lists: "Списки",
        proto: "Протокол",
        dscp: "DSCP",
        sourceIp: "Исходный IP",
        destinationIp: "IP назначения",
        sourcePort: "Исходный порт",
        destinationPort: "Порт назначения",
      },
    },
    routingRuleUpsert: {
      createTitle: "Создать правило маршрутизации",
      editTitle: "Изменить правило маршрутизации",
      description:
        "Это правило направляет подходящий трафик в указанный outbound.",
      cardDescription:
        "Выберите списки и outbound, затем при необходимости сузьте правило по протоколу, портам и адресам.",
      messages: {
        saved:
          "Правило маршрутизации сохранено в черновик. Примените новый конфиг, чтобы записать его.",
      },
      missing: {
        cardDescription: "Запрошенное правило маршрутизации не найдено.",
        cardTitle: "Правило не найдено",
        description:
          "Вернитесь к таблице правил маршрутизации и выберите корректную запись.",
        back: "Назад к правилам маршрутизации",
      },
      validation: {
        atLeastOneCondition:
          "Укажите хотя бы одно условие: список, DSCP, адрес источника/назначения или порт источника/назначения.",
        dscpRange: "DSCP должен быть целым числом от 1 до 63.",
        outboundRequired: "Тег outbound обязателен.",
      },
      actions: { create: "Создать правило", save: "Сохранить правило" },
      fields: {
        mode: "Тип правила",
        ruleType: "Типы правил",
        modeOptions: {
          normal: "Обычное правило",
          ipv4: "Шлюз по умолчанию IPv4",
          ipv6: "Шлюз по умолчанию IPv6",
        },
        modeHint:
          "Правила шлюза по умолчанию сопоставляют нелокальный трафик одного семейства IP и не используют другие условия.",
        lists: "Списки",
        listsPlaceholderDescription:
          "Добавьте один или несколько настроенных списков для этого правила.",
        noListsSelected: "Списки не выбраны",
        listsHint: "Выберите, к каким спискам применяется это правило.",
        proto: "Протокол",
        any: "Любой",
        anyLower: "любой",
        protocol: "Протокол",
        protoHint:
          "Фильтр по протоколу (TCP, UDP и т.д.). Оставьте пустым для «любого».",
        dscp: "DSCP",
        dscpHint:
          "Фильтр по DSCP-метке пакета. Оставьте пустым для любого значения.",
        sourcePort: "Исходный порт",
        destinationPort: "Порт назначения",
        sourcePortHint:
          "Исходный порт(ы). Через запятую, диапазоны допустимы. Префикс `!` для отрицания.",
        destinationPortHint:
          "Порт(ы) назначения. Через запятую, диапазоны допустимы. Префикс `!` для отрицания.",
        sourceAddresses: "Исходные адреса",
        destinationAddresses: "Адреса назначения",
        sourceAddressHint:
          "Исходный IP/CIDR. Через запятую. Префикс `!` для отрицания.",
        destinationAddressHint:
          "IP/CIDR назначения. Через запятую. Префикс `!` для отрицания.",
        outbound: "Outbound",
        selectOutbound: "Выберите outbound",
        configuredOutbounds: "Настроенные outbounds",
        outboundHint: "Какой outbound должен обрабатывать подходящий трафик.",
      },
      placeholders: {
        dscp: "46",
        sourcePort: "80,443 или 10000-20000",
        destinationPort: "443 или !53,123",
        sourceAddresses: "192.168.1.10,10.0.0.0/8",
        destinationAddresses: "2001:db8::1 или !203.0.113.0/24",
      },
    },
    outbounds: {
      title: "Outbounds (выходы)",
      description: "Настроенные outbounds и группы urltest.",
      actions: { new: "Добавить outbound" },
      bulk: {
        selected: "Выбрано: {{count}}",
        delete: "Удалить выбранные",
        confirmDelete:
          "Удалить {{count}} outbound(ов)? Связи проверяются только при сохранении.",
      },
      deleteDialog: {
        title: "Удалить outbound?",
        description:
          "При подтверждении операции будут произведены следующие действия:",
        confirm: "Удалить",
        items: {
          outboundPrefix: "Outbound",
          outboundSuffix: "будет удалён.",
          dependentOutboundPrefix: "Зависимый urltest outbound",
          dependentOutboundSuffix: "будет удалён.",
          routingRule: "Правило маршрутизации #{{number}} будет удалено.",
          ruleDetail: "{{label}}: {{value}}",
          dnsDetour: 'DNS-сервер "{{server}}" будет изменён.',
          urltestGroupChanged:
            'Группа #{{group}} тестового outbound "{{outbound}}" будет изменена.',
          urltestGroupRemoved:
            'Группа #{{group}} тестового outbound "{{outbound}}" будет удалена.',
          groupOutbounds: "Outbounds",
        },
      },
      empty: {
        title: "Outbounds пока нет",
        description:
          "Добавьте outbound, чтобы начать строить поведение маршрутизации.",
      },
      headers: {
        tag: "Название",
        type: "Тип",
        summary: "Детали",
        runtime: "Состояние",
        actions: "Действия",
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
          'Outbound "{{outbound}}" ссылается на отсутствующий тег "{{referenced}}".',
      },
    },
    outboundUpsert: {
      typeHints: {
        interface:
          "Трафик уходит через выбранный сетевой интерфейс (tun0, wg0, eth0) с необязательными шлюзами.",
        table:
          "Трафик отправляется по существующей таблице маршрутизации ядра по её ID.",
        urltest:
          "Группа outbound’ов: сервис периодически скачивает URL через каждый и выбирает самый быстрый.",
        icmptest:
          "То же, но каждый кандидат проверяется ping-ом (ICMP echo) на свой адрес.",
        blackhole: "Весь трафик, входящий в этот outbound, отбрасывается.",
        ignore:
          "Весь трафик, входящий в этот outbound, игнорирует последующие правила keen-pbr и маршрутизируется согласно настройкам роутера.",
      },
      advanced: {
        probesTitle: "Проверки и повторы",
        circuitBreakerTitle: "Circuit breaker",
        hasError: "есть ошибки",
        changed: "изменено",
        default: "по умолчанию",
      },
      conntrack: {
        label: "Соединения при переключении",
        hint: "Что делать с установленными соединениями, когда работающий outbound заменяется более быстрым или приоритетным. Соединения через упавший outbound сбрасываются всегда.",
        preserve: "Сохранять",
        delete: "Сбрасывать",
      },
      ladder: {
        phrase: {
          priority: "Весь трафик идёт через самый быстрый работающий outbound ступени 1.",
          balance:
            "Новые соединения распределяются между работающими outbound’ами ступени 1 по их весам.",
          fallbackOne: "Если ни один из них не работает — берётся ступень 2.",
          fallbackMany:
            "Если ни один из них не работает — по очереди берутся ступени 2–{{last}}.",
        },
        primaryStep: "Основная ступень",
        backupStep: "Резервная ступень {{index}}",
        activeNow: "сейчас работает",
        ifAllDown: "если все недоступны",
        moveUp: "Выше",
        moveDown: "Ниже",
        removeStep: "Удалить ступень",
        removeMember: "Убрать {{tag}}",
        add: "Добавить",
        addStep: "Добавить резервную ступень",
        noOptions: "Все доступные outbound’ы уже использованы",
        weight: "Вес",
        weightHint:
          "Доля новых соединений этого outbound внутри ступени. Например, веса 7 и 3 дают 70% и 30%. Пусто — вес 1. Учитываются только работающие outbound’ы: если один недоступен, его доля делится между остальными по их весам. Уже установленные соединения не переносятся. Допустимо от 1 до 100.",
        shareTitle: "Доля новых соединений, когда все outbound’ы ступени работают",
        pingTarget: "Цель ping",
        latency: "{{value}} мс",
        roles: {
          selected: "выбран",
          balanced: "в ротации",
          standby: "в запасе",
          waiting: "ожидает",
          degraded: "перебои",
          unavailable: "недоступен",
        },
      },
      createTitle: "Создать outbound",
      editTitle: "Изменить outbound",
      editCardTitle: "Изменить {{tag}}",
      description:
        "Outbound может быть сетевым интерфейсом, таблицей маршрутизации или группой urltest, которая выбирает самый быстрый вариант.",
      cardDescription: "Настройте interface или urltest outbound.",
      missing: {
        cardDescription: "Запрошенный outbound не найден.",
        cardTitle: "Outbound не найден",
        description:
          "Вернитесь к таблице outbounds и выберите корректную запись.",
        back: "Назад к outbounds",
      },
      actions: { create: "Создать outbound", save: "Сохранить outbound" },
      strategy: {
        cards: {
          priority: { title: "Самый быстрый", description: "Выбирает выход с наименьшей задержкой" },
          balance: { title: "Многопутевая передача", description: "Балансировка подключений между выходами активной ступени" },
        },
        label: "Стратегия выбора",
        hint: "Priority оставляет один выбранный outbound; balance распределяет новые соединения между исправными outbound (недоступно на Keenetic).",
        hintKeenetic: "Балансировка нагрузки отключена на Keenetic; используйте встроенные функции многопутевой маршрутизации маршрутизатора.",
        options: {
          priority: "По приоритету",
          balance: "Балансировка",
        },
      },
      fields: {
        aboutType: "Об этом типе",
        tag: "Название",
        tagHint:
          "Уникальное название для этого outbound. Используется в правилах и группах.",
        type: "Тип",
        outboundTypes: "Типы outbound",
        typeOptions: {
          interface: "Интерфейс",
          table: "Таблица маршрутизации",
          urltest: "Автовыбор (urltest)",
          icmptest: "Автовыбор (ICMP)",
          blackhole: "Blackhole",
          ignore: "Ignore",
        },
      },
      interface: {
        gatewayPlaceholder: "необязательно, напр. auto или 10.23.0.1",
        gateway6Placeholder: "необязательно, напр. auto или fe80::1",
        gatewaysHint: 'Укажите "auto", чтобы попытаться определить автоматически',
        title: "Настройки интерфейса",
        description:
          "Укажите исходящий интерфейс и необязательные IPv4/IPv6 шлюзы для этого outbound.",
        interface: "Интерфейс",
        interfacePlaceholder: "Выберите или введите интерфейс",
        interfaceHint:
          "Имя исходящего интерфейса, напр. `tun0`, `eth0`, `wg0`.",
        gateway: "Шлюз по умолчанию IPv4",
        gatewayHint:
          "Необязательный IPv4-шлюз; `auto` автоматически выбирает его из основного маршрута по умолчанию.",
        gateway6: "Шлюз по умолчанию IPv6",
        gateway6Hint:
          "Необязательный IPv6-шлюз; `auto` автоматически выбирает его из основного маршрута по умолчанию.",
      },
      table: {
        title: "Настройки таблицы маршрутизации",
        description:
          "Привязать этот outbound к существующей таблице маршрутизации ядра.",
        field: "ID таблицы",
        hint: "ID таблицы маршрутизации ядра для этого outbound.",
      },
      blackhole: {
        title: "Поведение blackhole",
        description:
          "Outbounds типа blackhole намеренно отбрасывают весь подходящий трафик.",
      },
      ignore: {
        title: "Поведение ignore",
        description:
          "Outbounds типа ignore пропускают подходящий трафик без изменения policy-based routing.",
      },
      urltest: {
        probeTimeout: "Таймаут проверки (мс)",
        probeTimeoutHint: "Сколько ждать ответа на каждый запрос проверки (в миллисекундах).",
        groupsTitle: "Группы outbound (urltest)",
        groupsDescription:
          "Добавьте outbounds в группу. Самый быстрый outbound (по urltest-проверке) будет выбран автоматически.",
        groupTitle: "Группа {{index}}",
        groupDescription:
          "Приоритет {{index}} - группы с более высоким приоритетом предпочтительнее.",
        interfaceOutbounds: "Interface outbounds",
        addOutbound: "Добавить outbound",
        noInterfaceOutbounds: "Interface outbounds не найдены.",
        addInterfaceOutboundsFirst:
          "Сначала добавьте interface outbounds, чтобы у групп urltest были цели для выбора.",
        addGroup: "Добавить группу",
        probingTitle: "Проверки и повторы",
        probingDescription:
          "Настройте, как группа urltest проверяет кандидатов и повторяет неудачные проверки.",
        probeUrl: "URL проверки",
        probeUrlHint:
          "Сервис загружает этот URL с заданным интервалом, чтобы проверить доступность интерфейса и измерить задержку.",
        interval: "Интервал (мс)",
        intervalHint: "Как часто запрашивать Probe URL (в миллисекундах).",
        tolerance: "Допуск (мс)",
        toleranceHint:
          "Не переключать outbound, если разница задержки не превышает это значение. Предотвращает флаппинг.",
        retryAttempts: "Число повторов",
        retryAttemptsHint:
          "Дополнительные попытки проверки перед тем, как считать outbound неработающим.",
        retryInterval: "Интервал повтора (мс)",
        retryIntervalHint:
          "Задержка между повторами после неудачной проверки (в миллисекундах).",
      },
      icmptest: {
        title: "ICMP-проверки",
        description:
          "Настройте строгие ICMP echo-проверки. Для каждого кандидата нужен отдельный IPv4- или IPv6-адрес.",
        targetHint:
          "IPv4- или IPv6-адрес, который пингуется через этот outbound.",
        targetLabel: "Цель ping для {{outbound}}",
        targetsEmpty:
          "Добавьте кандидатов в группу outbound, чтобы настроить их цели.",
        count: "Пакетов за проверку",
        countHint:
          "От 1 до 10 последовательных ICMP-попыток для каждого кандидата.",
        maxFailed: "Допустимо ошибок",
        maxFailedHint:
          "Кандидат успешен, только если число ошибок не превышает это значение.",
        packetInterval: "Пауза между попытками (мс)",
        packetIntervalHint:
          "Пауза после завершения попытки перед следующей (100–1000 мс).",
        probeTimeout: "Таймаут ответа (мс)",
        probeTimeoutHint:
          "Максимальное ожидание строго сопоставленного echo reply (100–5000 мс).",
        maxRtt: "Максимальный RTT (мс)",
        maxRttHint: "Более медленные ответы считаются неуспешными пакетами.",
        interval: "Интервал sweep (мс)",
        intervalHint:
          "Должен покрывать worst-case всех последовательных проверок и запас 25%.",
        tolerance: "Допуск выбора (мс)",
        toleranceHint:
          "Сохранять текущего кандидата, пока его RTT отличается от лучшего не больше этого значения.",
      },
      circuitBreaker: {
        title: "Circuit breaker - ограничение проверок при устойчивых сбоях",
        description:
          "Предотвращает избыточные проверки, когда интерфейс или URL проверки устойчиво недоступен.",
        failures: "Ошибок до открытия",
        failuresHint:
          "Открыть circuit после такого числа последовательных сбоев.",
        successes: "Успехов до закрытия",
        successesHint: "Число успешных проверок для закрытия circuit.",
        timeout: "Таймаут открытия (мс)",
        timeoutHint:
          "Как долго circuit остаётся открытым до начала half-open проверок (в мс).",
        halfOpen: "Half-open проверки",
        halfOpenHint:
          "Количество попыток проверки в фазе half-open, прежде чем circuit полностью закроется или откроется снова.",
      },
      killSwitch: {
        title: "Kill-switch",
        description:
          "Что делать с трафиком этого outbound, пока его интерфейс или шлюз недоступен.",
        inheritNow: "Сейчас: {{value}}",
        options: {
          inherit: { title: "Поведение по умолчанию" },
          off: {
            title: "Не блокировать",
            description: "Трафик пойдёт в обход, через основной маршрут.",
          },
          reject: {
            title: "Блокировать с ошибкой",
            description:
              "Соединения сразу завершаются ошибкой (unreachable): приложения узнают об этом мгновенно.",
          },
          drop: {
            title: "Блокировать молча",
            description:
              "Пакеты отбрасываются (blackhole): приложения ждут тайм-аута.",
          },
        },
        badge: {
          inherit: "по умолчанию",
          off: "не блокировать",
          reject: "блокировать с ошибкой",
          drop: "блокировать молча",
        },
      },
      validation: {
        tagRequired: "Тег обязателен.",
        duplicateTag: 'Тег outbound "{{tag}}" уже существует.',
        missingReference:
          'Outbound "{{outbound}}" ссылается на отсутствующий тег "{{referenced}}".',
      },
    },
    dnsRules: {
      title: "DNS-правила",
      description:
        "Определяет, какой DNS-сервер используется для доменов из ваших списков.",
      actions: {
        add: "Добавить DNS-правило",
        enableRule: "Включить правило",
        disableRule: "Выключить правило",
      },
      bulk: {
        selected: "Выбрано: {{count}}",
        enable: "Включить выбранные",
        disable: "Выключить выбранные",
        delete: "Удалить выбранные",
        confirmDelete: "Удалить {{count}} DNS-правил(о/а)?",
      },
      messages: {
        saved:
          "Конфигурация DNS сохранена в черновик. Примените новый конфиг, чтобы записать её.",
      },
      validation: {
        invalidFallback:
          "Основные DNS сервера должны ссылаться на существующие теги серверов.",
        invalidFallbackChange:
          "Нельзя изменить fallback, пока DNS-правила невалидны.",
        invalidResult:
          "Нельзя сохранить, потому что итоговые DNS-правила невалидны.",
      },
      fallback: {
        title: "Резервные DNS-серверы",
        description:
          "Упорядоченный список DNS-серверов, которые dnsmasq использует, когда ни одно DNS-правило не подходит. Если список задан, dnsmasq игнорирует системные upstream-серверы (no-resolv).",
        add: "Добавить резервный DNS-сервер",
        placeholderTitle: "Резервные DNS-серверы не выбраны",
        placeholderDescription:
          "Необязательно. Оставьте пустым, чтобы для доменов без правил использовались системные upstream-серверы.",
        noneDefined: "На странице DNS-серверы не добавлено ни одного сервера.",
        noneAvailable: "Все DNS-серверы уже выбраны.",
      },
      integration: {
        title: "Управлять dnsmasq (DNS-серверы для списков)",
        description:
          "Если включено, keen-pbr генерирует конфигурацию dnsmasq, чтобы домены из ваших списков разрешались через выбранные DNS-серверы.",
        effectiveHint:
          "Режим явно не задан; он включён автоматически, потому что есть DNS-правила.",
        inactiveTitle: "DNS-правила неактивны",
        inactiveDescription:
          "Управление dnsmasq выключено, поэтому правила ниже сохранены, но не применяются. Включите его выше, чтобы они заработали.",
      },
      empty: {
        title: "DNS-правил пока нет",
        description:
          "Правил пока нет - добавьте правило, чтобы направлять DNS-запросы по спискам через выбранный сервер.",
      },
      headers: {
        criteria: "Условие",
        serverTag: "DNS-сервер",
        allowDomainRebinding: "Разрешение rebind",
        actions: "Действия",
      },
      criteriaLabels: {
        lists: "Списки",
      },
      rebinding: {
        enabled: "Разрешён",
        disabled: "Запрещён",
      },
    },
    dnsRuleUpsert: {
      createTitle: "Создать DNS-правило",
      editTitle: "Изменить DNS-правило",
      description:
        "Это правило определяет, какой DNS-сервер использовать для доменов из конкретного списка.",
      cardDescription: "Укажите имена списков и DNS-сервер для этого правила.",
      messages: {
        saved:
          "DNS-правило сохранено в черновик. Примените новый конфиг, чтобы записать его.",
      },
      validation: {
        notFound: "Запрошенное DNS-правило не найдено.",
        fixErrors: "Исправьте ошибки валидации перед сохранением.",
        serverRequired: "Правило должно ссылаться на существующий DNS-сервер.",
        listsRequired: "Правило должно содержать хотя бы один список.",
        unknownLists: "Неизвестные списки: {{lists}}",
        duplicate: "Дублирующееся правило.",
      },
      missing: {
        cardDescription: "Запрошенное DNS-правило не найдено.",
        cardTitle: "DNS-правило не найдено",
        description: "Вернитесь к DNS-правилам и выберите корректную запись.",
        back: "Назад к DNS-правилам",
      },
      actions: { create: "Создать правило", save: "Сохранить правило" },
      fields: {
        serverTag: "DNS-сервер",
        selectServer: "Выберите DNS-сервер",
        dnsServers: "DNS-серверы",
        noServers: "На странице DNS-серверы не добавлено ни одного сервера.",
        listNames: "Списки доменов",
        allowDomainRebinding: "Разрешить DNS rebind для этих доменов",
        allowDomainRebindingHint:
          "Включайте только если вы точно знаете, что этот список доменов указывает на внутренние сервисы. Тогда ответы для подходящих доменов могут содержать внутренние/приватные IP-адреса (например, 192.168.0.0/16, 10.0.0.0/8 и другие диапазоны локальной сети).",
        listPlaceholderDescription:
          "Выберите списки для этого правила. Совпадающие домены будут использовать этот DNS-сервер.",
        noListsSelected: "Списки не выбраны",
        noLists:
          "Не найдено ни одного списка. Пожалуйста, сначала создайте его на странице Списки.",
      },
    },
    lists: {
      title: "Списки",
      description:
        "Группы доменов и IP-адресов для использования в правилах трафика и DNS.",
      actions: {
        new: "Добавить список",
        update: "Обновить",
        updateAll: "Обновить все",
      },
      empty: {
        title: "Списков пока нет",
        description:
          "Создайте первый список, чтобы использовать его в правилах маршрутизации и DNS.",
      },
      headers: {
        name: "Имя",
        type: "Тип",
        stats: "Записи",
        rules: "Исп. в правилах",
        actions: "Действия",
      },
      delete: {
        confirm: 'Удалить список "{{name}}"?',
        confirmWithReferences:
          'Удалить список "{{name}}" и убрать его ссылки из правил маршрутизации и DNS?',
      },
      deleteDialog: {
        title: "Удалить списки?",
        description:
          "При подтверждении операции будут произведены следующие действия:",
        confirm: "Удалить",
        items: {
          listPrefix: "Список",
          listSuffix: "будет удалён.",
          routeRuleRemoved: "Правило маршрутизации #{{number}} будет удалено.",
          routeRuleUpdated: "Правило маршрутизации #{{number}} будет изменено.",
          dnsRuleRemoved: "DNS-правило #{{number}} будет удалено.",
          dnsRuleUpdated: "DNS-правило #{{number}} будет изменено.",
        },
      },
      bulk: {
        selected: "Выбрано: {{count}}",
        refreshSelected: "Обновить выбранные (URL)",
        deleteSelected: "Удалить выбранные списки",
        confirmDeleteSimple: "Удалить списки: {{names}}?",
        confirmDeleteWithRefs:
          "Удалить списки: {{names}} и при необходимости убрать ссылки из правил маршрутизации и DNS?",
        noUrlBacked:
          "Ни один выбранный список не основан на URL (обновлять нечего).",
      },
      location: {
        inline: "Встроенный",
      },
      refresh: {
        draftBlocked:
          "Примените черновик конфигурации перед обновлением списков.",
        updateDisabled: "Примените черновик перед обновлением",
      },
      rule: {
        configured: "Настроен",
      },
      messages: {
        refreshedOne: "Обновление списка завершено.",
        refreshedAll: "Обновление списков завершено.",
        refreshFailedOne:
          'Список "{{names}}" не удалось обновить. Подробности смотрите в логах.',
        refreshFailedMany:
          "Не удалось обновить {{count}} списков: {{names}}. Подробности смотрите в логах.",
        refreshFailedMore: "ещё {{count}}",
      },
      lastUpdated: "Последнее обновление: {{value}}",
      neverUpdated: "Ещё не обновлялся",
      noStats: "-",
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
        "Список может содержать домены и IP, введённые вручную, загруженные по URL или из файла.",
      cardDescription:
        "Проверьте источник списка, TTL и содержимое перед сохранением.",
      messages: {
        created:
          "Список сохранён в черновик. Примените новый конфиг, чтобы записать его.",
        updated:
          "Изменения списка сохранены в черновик. Примените новый конфиг, чтобы записать их.",
      },
      missing: {
        cardDescription: "Запрошенный список не найден.",
        cardTitle: "Список не найден",
        description:
          "Вернитесь к таблице списков и выберите корректную запись.",
        back: "Назад к спискам",
      },
      actions: {
        saving: "Сохранение...",
        create: "Создать список",
        save: "Сохранить список",
      },
      common: {
        title: "Параметры списка",
        description: "Задайте идентификатор списка перед выбором источника.",
      },
      sourceSwitcher: {
        title: "Тип источника",
        description:
          "Выберите источник для редактирования. Старые списки с несколькими сохранёнными источниками останутся видимыми, пока вы не переключитесь.",
        confirmChange:
          "Переключить тип источника и очистить заполненные сейчас поля?",
      },
      sourceGroups: {
        url: {
          button: "URL",
          title: "Удалённый URL",
          description:
            "Загружает записи списка с удалённой точки по HTTP или HTTPS и задаёт время жизни кэша для разрешённых IP.",
        },
        file: {
          button: "Файл на устройстве",
          title: "Локальный файл",
          description: "Читает записи списка из файла, доступного на роутере.",
        },
        inline: {
          button: "Домены / IP",
          title: "Домены / IP",
          description: "Позволяет указать домены и IP-адреса прямо в конфиге.",
        },
      },
      fields: {
        name: "Имя",
        nameHint: "Стабильный идентификатор для использования в правилах.",
        ttlMs: "Время жизни IP-кэша (мс)",
        ttlMsHint:
          "Как долго хранить разрешённые IP в ipset. `0` = без таймаута.",
        detour: "Делать запросы через Outbound",
        detourEmpty: "Не выбрано",
        detourPlaceholder: "Необязательный тег outbound",
        detourHint:
          "Необязательный outbound для загрузки этого списка по удалённому URL.",
        url: "Удалённый URL",
        urlHint:
          "Необязательно: URL для загрузки записей. Объединяется с остальным содержимым.",
        file: "Абсолютный путь к файлу",
        fileHint:
          "Необязательно: путь к файлу на устройстве. Объединяется с другими источниками.",
        domains: "Домены",
        domainsHint:
          "Домены, по одному в строке. `example.com` автоматически включает все поддомены.",
        ipCidrs: "IP CIDR",
        ipCidrsHint:
          "IP-адреса или диапазоны CIDR, по одному в строке. Напр. `93.184.216.34`, `10.0.0.0/8`.",
      },
      validation: {
        nameRequired: "Имя обязательно.",
        duplicateName: "Список с таким именем уже существует.",
        invalidTtl: "TTL должен быть неотрицательным целым числом.",
      },
    },
  },
} as const
