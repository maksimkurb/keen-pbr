export const ruTranslation = {
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
        draftChanged:
          "Конфигурация была изменена. Сохраните её на диск для применения.",
        actions: {
          applying: "Применение...",
          apply: "Применить",
          restarting: "Перезапуск...",
          restart: "Перезапустить",
        },
        compact: {
          resolverStale: "Конфиг dnsmasq устарел.",
        },
        full: {
          unsavedTitle: "Конфигурация не сохранена",
          staleTitle: "dnsmasq использует устаревший конфиг резолвера",
          staleDescription:
            "Ожидаемый хеш резолвера ({{expected}}…) не совпадает с активным хешем dnsmasq ({{actual}}…).",
        },
      },
      overview: {
        pageDescription:
          "Обзор состояния маршрутизации, конфигурации и активных outbounds",
        runtime: {
          title: "Маршрутизация",
          description: "Управление policy-based routing.",
          loadError: "Не удалось загрузить состояние маршрутизации.",
          version: "Версия",
          status: "Статус маршрутизации",
          dnsmasqGood: "dnsmasq в порядке",
          dnsmasqStale: "dnsmasq требуется перезапуск",
          actions: {
            start: "Запустить",
            stop: "Остановить",
            restart: "Перезапустить",
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
        dnsCheck: {
          card: {
            title: "Проверка DNS",
            description:
              "Проверяет, что DNS-разрешение через keen-pbr работает корректно - из этого браузера или с другого устройства.",
            disabledDescription:
              "Включите `config.dns.dns_test_server`, чтобы запустить встроенную самопроверку DNS.",
            configuredServers: "Настроенные DNS-серверы",
            noServers:
              "На странице DNS-серверов не определено ни одного DNS-сервера.",
            via: "через {{detour}}",
            checking: "Проверка...",
            runAgain: "Запустить снова",
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
            disabled: "Встроенный DNS-пробник отключён в конфиге.",
            browserSuccess: "DNS-запрос из браузера достиг dnsmasq.",
            manualProbeSuccess: "DNS-запрос от устройства достиг dnsmasq.",
            browserProbeFail:
              "Запрос браузера завершился, но DNS-пробник не увидел lookup.",
            sseUnavailable:
              "Поток событий DNS в реальном времени недоступен, поэтому проверка не смогла запуститься.",
            browserFail:
              "Запрос браузера выполнился, но DNS lookup не был замечен.",
            sseFail: "Поток событий DNS в реальном времени не подключён.",
            browserChecking: "Проверяем DNS-путь браузера...",
            browserUnknown: "Статус DNS в браузере пока неизвестен.",
            manualSuccess: "DNS-запрос от устройства достиг dnsmasq.",
            manualWaiting: "Ожидание вашей ручной команды nslookup...",
            manualIncomplete: "Ручной тест устройства ещё не завершён.",
          },
        },
        routingTest: {
          title: "Куда пойдёт этот трафик?",
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
          hostLabel: 'Хост "{{target}}"',
          inRuleLists: "Есть в доменных/IP-списках правила?",
        },
        routingLegend: {
          title: "Условные обозначения",
          inLists: "Есть в доменных/IP-списках",
          notInLists: "Нет в доменных/IP-списках",
          inIpsetAndLists: "Есть в IPSet и в списках",
          notInIpsetAndNotInLists: "Нет в IPSet и нет в списках",
          inIpsetButShouldNotBe: "Есть в IPSet, хотя не должно",
          notInIpsetButShouldBe: "Нет в IPSet, хотя должно быть",
        },
      },
      pages: {
        settings: {
          title: "Настройки",
          description:
            "Глобальные настройки, действующие на все outbounds и правила.",
          saved:
            "Настройки сохранены в черновик. Примените новый конфиг, чтобы записать их.",
          general: {
            title: "Общие",
            description: "Поведение по умолчанию для всех outbounds.",
            strictEnforcementLabel:
              "Блокировать трафик при падении outbound (kill-switch)",
            strictEnforcementHint:
              "Если VPN или интерфейс отключится, трафик по его правилам будет заблокирован, а не отправлен через основную таблицу маршрутизации. Можно переопределить для каждого outbound.",
          },
          autoupdate: {
            title: "Автообновление списков",
            description: "Автоматическое обновление удалённых списков.",
            enabledLabel: "Включить автообновление списков",
            enabledHint:
              "Автоматически скачивать обновления удалённых списков и обновлять маршрутизацию при изменениях.",
            cronLabel: "Расписание обновления",
            cronHintPrefix:
              "Как часто проверять обновления. Формат cron - используйте",
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
          },
          actions: {
            saving: "Сохранение...",
            save: "Сохранить",
          },
        },
        dnsServers: {
          title: "DNS-серверы",
          description: "Upstream DNS-серверы для разрешения доменных имён.",
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
            "Настройте адрес сервера и необязательный detour outbound.",
          editCardTitle: "Изменить {{tag}}",
          fields: {
            tag: "Название",
            tagHint: "Короткое название сервера для использования в DNS-правилах.",
            address: "Адрес",
            addressPlaceholder: "1.1.1.1 или [2606:4700::1111]:53",
            addressHint:
              "IP-адрес сервера, напр. `1.1.1.1` или `[2606:4700::1111]:53`.",
            detour: "Outbound",
            detourEmpty: "Не выбрано",
            detourPlaceholder: "Необязательный тег outbound",
            detourHint:
              "Необязательно: отправлять DNS-запросы к этому серверу через конкретный outbound (напр. VPN).",
          },
          validation: {
            tagRequired: "Название обязательно.",
            tagUnique: "Название должно быть уникальным.",
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
          actions: { addRule: "Добавить правило маршрутизации" },
          messages: {
            saved:
              "Правила маршрутизации сохранены в черновик. Примените новый конфиг, чтобы записать их.",
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
            selectList: "Выберите хотя бы один список.",
            outboundRequired: "Тег outbound обязателен.",
          },
          actions: { create: "Создать правило", save: "Сохранить правило" },
          fields: {
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
            sourcePort: "80,443 или 10000-20000",
            destinationPort: "443 или !53,123",
            sourceAddresses: "192.168.1.10,10.0.0.0/8",
            destinationAddresses: "2001:db8::1 или !203.0.113.0/24",
          },
        },
        outbounds: {
          title: "Интерфейсы / выходы",
          description: "Настроенные outbounds и группы urltest.",
          actions: { new: "Добавить outbound" },
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
            table: "table={{value}}",
            urltest: "outbounds={{value}}",
          },
          messages: {
            missingReference:
              'Outbound "{{outbound}}" ссылается на отсутствующий тег "{{referenced}}".',
          },
        },
        outboundUpsert: {
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
          common: {
            noExtraFields:
              "Для этого типа не нужны дополнительные поля, кроме тега outbound.",
          },
          fields: {
            tag: "Название",
            tagHint:
              "Уникальное название для этого outbound. Используется в правилах и группах.",
            type: "Тип",
            outboundTypes: "Типы outbound",
            typeOptions: {
              interface: "Интерфейс",
              table: "Таблица маршрутизации",
              urltest: "Автовыбор (urltest)",
              blackhole: "Blackhole",
              ignore: "Ignore",
            },
          },
          interface: {
            title: "Настройки интерфейса",
            description:
              "Укажите исходящий интерфейс и необязательный шлюз для этого outbound.",
            interface: "Интерфейс",
            interfaceHint:
              "Имя исходящего интерфейса, напр. `tun0`, `eth0`, `wg0`.",
            gateway: "Шлюз",
            gatewayHint: "Необязательный IP-адрес шлюза для этого outbound.",
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
              "Outbounds типа ignore пропускают подходящий трафик без изменений policy-based routing.",
          },
          urltest: {
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
          strictEnforcement: {
            label: "Переопределение kill-switch",
            hint: "Переопределяет глобальную настройку kill-switch для этого outbound.",
            default: "По умолчанию (как в глобальном конфиге)",
            enabled: "Включено",
            disabled: "Выключено",
          },
          validation: {
            duplicateTag: 'Тег outbound "{{tag}}" уже существует.',
            missingReference:
              'Outbound "{{outbound}}" ссылается на отсутствующий тег "{{referenced}}".',
          },
        },
        dnsRules: {
          title: "DNS-правила",
          description:
            "Определяет, какой DNS-сервер используется для доменов из ваших списков.",
          actions: { add: "Добавить DNS-правило" },
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
            title: "Основные DNS сервера",
            description:
              "Упорядоченный список DNS-серверов, которые dnsmasq использует, когда ни одно DNS-правило не подходит.",
            add: "Добавить основной DNS сервер",
            placeholderTitle: "Основные DNS сервера не выбраны",
            placeholderDescription:
              "Добавьте один или несколько DNS-серверов. Их порядок сохраняется и используется в сгенерированном конфиге dnsmasq.",
            noneDefined: "В config.dns.servers не определены DNS-серверы.",
            noneAvailable: "Все DNS-серверы уже выбраны.",
          },
          empty: {
            title: "DNS-правил пока нет",
            description:
              "Правил пока нет - добавьте правило, чтобы направлять DNS-запросы по спискам через выбранный сервер.",
          },
          headers: {
            lists: "Списки",
            serverTag: "DNS-сервер",
            actions: "Действия",
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
            listsRequired: "Правило должно содержать хотя бы одно имя списка.",
            unknownLists: "Неизвестные имена списков: {{lists}}",
            duplicate: "Дублирующееся правило.",
          },
          missing: {
            cardDescription: "Запрошенное DNS-правило не найдено.",
            cardTitle: "DNS-правило не найдено",
            description: "Вернитесь к DNS Rules и выберите корректную запись.",
            back: "Назад к DNS-правилам",
          },
          actions: { create: "Создать правило", save: "Сохранить правило" },
          fields: {
            serverTag: "DNS-сервер",
            selectServer: "Выберите DNS-сервер",
            dnsServers: "DNS-серверы",
            noServers: "В config.dns.servers не определены DNS-серверы.",
            listNames: "Имена списков",
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
          location: {
            inline: "Встроенный",
          },
          refresh: {
            draftBlocked:
              "Примените или сбросьте сохранённый черновик перед обновлением URL-списков.",
            updateDisabled:
              "Примените или сбросьте черновик перед обновлением",
          },
          rule: {
            configured: "Настроен",
          },
          messages: {
            refreshedOne: "Обновление списка завершено.",
            refreshedAll: "Обновление списков завершено.",
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
