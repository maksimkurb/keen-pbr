---
title: REST API
weight: 5
aliases:
  - /docs/advanced/rest-api/
---

REST API нужен, чтобы управлять keen-pbr и следить за ним из скриптов и внешних систем. Например:

- Prometheus и Grafana показывают состояние VPN-каналов и задержку (см. [GET /metrics](#get-metrics)).
- Задание cron обновляет списки по расписанию через `POST /api/lists/refresh`.
- Скрипт перезапускает маршрутизацию через `POST /api/service/restart`.

Если вы пользуетесь только веб-интерфейсом, эту страницу можно не читать.

REST API доступен, если:
- Установлена полная версия пакета (`keen-pbr`, не `keen-pbr-headless`)
- В конфиге включён `api.enabled: true`
- При запуске не был передан флаг `--no-api`

## Конфигурация

В веб-интерфейсе откройте **Безопасность** > **Аутентификация и CORS**. Включите **Включить аутентификацию**, введите **Новый пароль** и подтвердите его. Если API должен принимать запросы из браузера с другого сайта, добавьте его адрес в **Разрешённые источники CORS** (по одному в строке). Изменения вступают в силу сразу.

Имя устройства задаётся в **Общее** > **Настройки** > **Общие** > **Имя устройства**. Оно показывается в заголовке вкладки браузера и под логотипом keen-pbr и помогает различать несколько роутеров.

Те же настройки в `config.json`:

```json { filename="config.json" }
{
  "device_name": "Домашний роутер",
  "api": {
    "enabled": true,
    "listen": "0.0.0.0:12121",
    "authentication": {
      "enabled": true,
      "password_hash": "argon2id$v=19$m=2048,t=2,p=1$..."
    },
    "cors": {
      "allowed_origins": ["https://panel.example.com"]
    }
  }
}
```

Параметр `device_name` необязателен и может быть пустым.

Если вы правите `config.json` вручную, хеш пароля получите командой (она спросит пароль):

```bash
keen-pbr hash-password
```

Чтобы сразу записать хеш в файл, выполните `keen-pbr --config /path/to/config.json hash-password --update`. После этого перезапустите сервис keen-pbr: демон не видит изменений, сделанных в файле снаружи.

Пароль хранится в виде хеша Argon2id. Хеши из старых версий keen-pbr не подходят: задайте пароль заново.

Когда аутентификация включена, запросы к API выполняются одним из двух способов:

- Basic-аутентификация: имя пользователя `admin` и ваш пароль (`curl -u admin:password ...`).
- Bearer-токен: временный ключ, который выдаёт `POST /api/auth/login` и который передаётся в заголовке `Authorization: Bearer <токен>`. Токен действует 24 часа. Одновременно действует только один токен: новый вход, в том числе в веб-интерфейсе, отменяет предыдущий. После перезапуска демона токен нужно получить заново.

Без входа доступны только `GET /api/auth/status` и `POST /api/auth/login`. `/metrics` защищён так же, как остальной API.

Если аутентификация выключена, запросы из браузера с других сайтов блокируются. Если включена, разрешены адреса из **Разрешённые источники CORS**, а также расширения Chrome и Firefox. Пароль передаётся по сети открытым текстом, поэтому используйте доверенную сеть или HTTPS через reverse proxy (веб-сервер перед keen-pbr).

По умолчанию API слушает `0.0.0.0:12121` (значение `api.listen`). Все маршруты доступны по этому адресу.

Полная схема запросов и ответов находится в [OpenAPI-документе](https://github.com/maksimkurb/keen-pbr/blob/main/docs/openapi.yaml). Основные маршруты (outbound — канал, через который идёт трафик: VPN-интерфейс, провайдер или блокировка):

| Маршрут | Назначение |
|---|---|
| `GET /api/health/service` | Состояние сервиса, черновика конфигурации, текущей операции запуска/остановки и отката. |
| `POST /api/service/start`, `/stop`, `/restart` | Асинхронный запуск, остановка или перезапуск маршрутизации. |
| `GET /api/config`, `POST /api/config` | Чтение или подготовка проверенного черновика конфигурации. |
| `GET /api/config/defaults` | Значения по умолчанию для настроек, которые не заданы в конфиге. |
| `POST /api/config/discard`, `/save`, `/rollback` | Отмена черновика, сохранение и применение либо откат к прежней конфигурации после неудачного применения. |
| `GET /api/status/events` | Поток событий Server-Sent Events (SSE): сервер сам присылает изменения, опрашивать его не нужно. События: `snapshot` (полное состояние при подключении), `service`, `outbounds`, `interfaces`, `issues`, `routing`, `auth`, `config`. Раз в 15 секунд приходит служебный сигнал, чтобы соединение не закрывалось. Событие `config` содержит только номер ревизии; сами данные читайте через `GET /api/config`. После обрыва соединения новое подключение снова начинается с `snapshot`. |
| `GET /api/health/issues` | Найденные проблемы и результаты проверок ядра: тот же список, что на странице обзора и в `keen-pbr status`. |
| `GET /api/runtime/outbounds`, `/api/runtime/interfaces` | Текущее состояние outbounds и список системных интерфейсов. |
| `GET /api/health/routing`, `POST /api/routing/test` | Диагностика маршрутизации и firewall и проверка маршрута для конкретного домена или IP-адреса. |
| `POST /api/lists/refresh` | Обновление одного или всех удалённых списков URL. |
| `POST /api/dns/servers/check` | Проверка DNS-серверов из применённой конфигурации: работают ли они и не подменяют ли ответы. |
| `GET /api/dns/test` | Поток событий перехвата DNS и TLS/QUIC-трафика либо проверка DNS в веб-интерфейсе. |
| `GET /api/diagnostics/command-failure` | Журнал недавних сбоев системных команд. |
| `GET /metrics` | Метрики Prometheus в текстовом формате. |
| `/api/auth/status`, `/api/auth/login`, `/api/auth/logout`, `/api/auth/password`, `/api/auth/settings` | Состояние аутентификации, вход и выход, пароль (`GET` показывает, задан ли он; `POST` задаёт новый), настройки аутентификации и CORS. |

---

## GET /api/health/service

Возвращает версию запущенного демона и состояние среды выполнения маршрутизации.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/service
```

### Ответ

```json
{
  "version": "3.4.0",
  "build": "20261010120000",
  "status": "running",
  "runtime_state": "running",
  "runtime_state_reason": "config apply complete",
  "os_type": "debian",
  "os_version": "12",
  "build_variant": "generic",
  "doh_supported": true,
  "config_is_draft": false,
  "rollback_available": false
}
```

Поле `status` принимает значения `running` (работает), `degraded` (перехват DNS включён, но недоступен) и `stopped` (остановлен). Поле `doh_supported` показывает, умеет ли эта сборка работать с DNS-серверами типа `doh`.

При включённом перехвате ответ содержит объект `intercept`: активность перехвата DNS (`dns_hold_active`) и анализа трафика (`l7_active`), доступные возможности ядра (`nfqueue`, `nflog`, `connbytes`, `addrtype`) и счётчики обработанных пакетов, ошибок и переполнений очередей ({{< term "NFQUEUE" >}}/NFLOG). Эти значения нужны в основном для диагностики.

Состояние каналов (здоровье, задержка) смотрите в `GET /api/runtime/outbounds`.

## GET /metrics

Возвращает метрики в текстовом формате Prometheus 0.0.4. Prometheus периодически обращается к этому адресу (scrape) и сохраняет значения. Аутентификация та же, что у API: используйте Basic-аутентификацию или текущий Bearer-токен. Если метрики недоступны, ответ `503`:

```bash
curl -u admin:password http://127.0.0.1:12121/metrics
```

Все длительности указаны в **секундах** (дробные числа). Если значение неизвестно, метрика **не публикуется**, а не выводится как `0` (см. «Когда серии не публикуются» ниже).

### Процесс и конфигурация

| Метрика | Тип | Метки | Смысл |
|---|---|---|---|
| `keen_pbr_build_info` | gauge | `version`, `commit`, `firewall_backend` | Всегда `1`; сведения о сборке. |
| `keen_pbr_process_start_time_seconds` | gauge | - | Unix-время старта; uptime: `time() - keen_pbr_process_start_time_seconds`. |
| `keen_pbr_active_rules` | gauge | - | Число применённых правил firewall. |
| `keen_pbr_config_reload_last_success_timestamp_seconds` | gauge | - | Unix-время последнего завершённого применения конфигурации (запуск тоже считается). До первого применения не публикуется. |
| `keen_pbr_config_reload_errors_total` | counter | - | Неудачные перезагрузки и применения конфигурации. |
| `keen_pbr_list_last_update_timestamp_seconds` | gauge | `list` | Unix-время последней успешной проверки удалённого списка (независимо от изменения содержимого). До первого успешного обновления после запуска не публикуется. |
| `keen_pbr_list_update_errors_total` | counter | `list` | Неудачные обновления удалённого списка. |

### Ошибки

`keen_pbr_errors_total{subsystem}` — счётчик ошибок по подсистемам. Выводятся все значения, включая нулевые: `firewall_apply` (применение правил firewall), `netlink` (настройка маршрутов), `set_write` (запись адресов в наборы), `conntrack`, `dns_parse` (разбор DNS-пакетов), `dns_tcp_partial` (неполные DNS-сообщения по TCP) и `dns_late_write` (неудачные отложенные записи после DNS-ответа).

### Пробы (urltest / icmptest)

Проба — периодическая проверка канала: запрос по URL (`urltest`) или ping (`icmptest`). Метки: `outbound` (проверяемый канал), `test_outbound` (группа проб), `interface`, `type` (`urltest` или `icmptest`).

| Метрика | Тип | Смысл |
|---|---|---|
| `keen_pbr_probe_attempts_total` | counter | Принятые результаты проб. |
| `keen_pbr_probe_successes_total` | counter | Успешные результаты проб. |
| `keen_pbr_probe_packets_sent_total` | counter | Отправленные {{< term "ICMP" >}} echo-запросы. Только `type="icmptest"`. |
| `keen_pbr_probe_packets_received_total` | counter | Полученные ICMP echo-ответы. Только `type="icmptest"`. |
| `keen_pbr_probe_up` | gauge | `1` — последняя проверка успешна, `0` — неуспешна. |
| `keen_pbr_probe_last_success_timestamp_seconds` | gauge | Unix-время последней успешной проверки. |
| `keen_pbr_probe_latency_seconds` | gauge | Задержка последней успешной проверки: для ICMP — среднее по полученным ответам, для URL — время запроса. Точность лучше миллисекунды. |
| `keen_pbr_probe_latency_min_seconds`, `keen_pbr_probe_latency_max_seconds` | gauge | Самый быстрый и самый медленный ответ последней успешной ICMP-проверки. Только `type="icmptest"`. |
| `keen_pbr_urltest_selected` | gauge | Метки `group`, `outbound`: `1` у выбранного сейчас outbound группы, `0` у остальных. |
| `keen_pbr_urltest_selection_changes_total` | counter | Метка `group`: число смен выбора с момента применения конфигурации (после применения счётчик обнуляется). |

#### Когда серии не публикуются

- `probe_up` — пока не завершилась первая проверка.
- `probe_latency_seconds`, `probe_latency_min_seconds`, `probe_latency_max_seconds` —
  если **последняя** проверка неуспешна или проверок ещё не было. В Grafana это разрыв
  линии, а не `0`.
- `probe_last_success_timestamp_seconds` — до первой успешной проверки.
- `list_last_update_timestamp_seconds` и
  `config_reload_last_success_timestamp_seconds` — до первого успеха.

### Interception

Метрики перехвата DNS и анализа трафика (L7). `path`: `dns` или `l7`.

| Метрика | Тип | Метки | Смысл |
|---|---|---|---|
| `keen_pbr_intercept_packets_total` | counter | `path` = `dns`, `l7` | Проверенные пакеты. |
| `keen_pbr_intercept_matches_total` | counter | `path` = `dns`, `l7` | Пакеты, совпавшие с доменами списков. |
| `keen_pbr_dns_hold_duration_seconds` | histogram | - | Время удержания DNS-ответа до вердикта. |
| `keen_pbr_dns_queue_wait_duration_seconds` | histogram | - | Ожидание DNS-пакета в очереди приёма. |
| `keen_pbr_dns_hold_timeouts_total` | counter | `cause` = `batch_budget`, `admission_blocked`, `own_write_slow`, `late_batch_full`, `other` | Удержания, дошедшие до дедлайна. |
| `keen_pbr_dns_late_writes_total` | counter | - | DNS-записи, завершённые после отпускания пакета. |
| `keen_pbr_queue_overruns_total` | counter | `queue` = `nfqueue`, `nflog` | Переполнения очередей ядра. |
| `keen_pbr_set_write_duration_seconds` | histogram | `path` = `dns`, `late_dns`, `l7` | Время записи в динамический набор. Границы (с): 0.0001, 0.00025, 0.0005, 0.001, 0.0025, 0.005, 0.01, 0.025, 0.05, 0.1, +Inf. |
| `keen_pbr_set_writes_total` | counter | `kind` = `add`, `refresh` | Добавленные или обновлённые по таймауту элементы. |
| `keen_pbr_set_refresh_total` | counter | `result` = `skipped`, `deferred`, `dropped` | Исходы обновления кэшированных элементов; `dropped` — очередь была заполнена. |
| `keen_pbr_set_cache_lookups_total` | counter | `result` = `hit`, `miss` | Обращения к кэшу наборов; попадание избавляет от записи до вердикта. |
| `keen_pbr_set_cache_entries` | gauge | - | Запомненные элементы динамических наборов. |
| `keen_pbr_conntrack_requests_total` | counter | - | Запросы очистки {{< term "conntrack" >}}. |
| `keen_pbr_conntrack_deleted_total` | counter | - | Удалённые записи conntrack. |

### Балансировка и маркированные пакеты

{{< only iptables >}} Эти метрики есть только при этом firewall-бэкенде. Если данных нет, серия не публикуется. После перестроения правил firewall счётчики начинаются с нуля.

| Метрика | Тип | Метки | Смысл |
|---|---|---|---|
| `keen_pbr_balance_classifications_total` | counter | `outbound`, `candidate`, `family` = `ipv4`, `ipv6` | Новые соединения, которые группа балансировки направила через канал `candidate`. |
| `keen_pbr_skip_marked_packets_total` | counter | `family` = `ipv4`, `ipv6` | Пакеты, которые keen-pbr пропустил, потому что они уже были помечены (**Общее** > **Настройки** > **Общие** > **Не обрабатывать маркированные пакеты**). |

Доля «медленных» записей вычисляется по bucket `le="0.025"` метрики
`keen_pbr_set_write_duration_seconds` (записи дольше 25 мс).

Конфигурация Prometheus:

```yaml
scrape_configs:
  - job_name: keen-pbr
    metrics_path: /metrics
    scrape_interval: 15s
    static_configs:
      - targets: ["router.example:12121"]
    basic_auth:
      username: admin
      password_file: /etc/prometheus/keen-pbr-password
```

Примеры запросов PromQL:

```promql
# Потери ICMP за 15 минут, % (нет данных, если пакеты не отправлялись)
100 * clamp(1 - increase(keen_pbr_probe_packets_received_total[15m])
    / (increase(keen_pbr_probe_packets_sent_total[15m]) > 0), 0, 1)

# Доля успешных проб за 15 минут
100 * increase(keen_pbr_probe_successes_total[15m])
    / (increase(keen_pbr_probe_attempts_total[15m]) > 0)

# Средняя и 95-я перцентиль задержки за час (секунды)
avg_over_time(keen_pbr_probe_latency_seconds[1h])
quantile_over_time(0.95, keen_pbr_probe_latency_seconds[1h])

# p99 времени удержания DNS (секунды)
histogram_quantile(0.99, sum by (le)
    (rate(keen_pbr_dns_hold_duration_seconds_bucket[5m])))

# Доля записей в наборы дольше 25 мс по путям
1 - sum by (path) (rate(keen_pbr_set_write_duration_seconds_bucket{le="0.025"}[5m]))
  / (sum by (path) (rate(keen_pbr_set_write_duration_seconds_count[5m])) > 0)

# Ошибки за час по подсистемам
sum by (subsystem) (increase(keen_pbr_errors_total[1h])) > 0
```

### DNS-прокси {#dns-proxy}

Метрики встроенного DNS-прокси, куда dnsmasq отправляет запросы к доменам списков с несколькими серверами или с DNS-over-HTTPS. Если прокси не используется, метрики не публикуются. Метки:

- `proxy_port` — порт прокси, он соответствует набору серверов;
- `server` — названия серверов (`tag`) через запятую;
- `upstream` — адрес сервера (`ip:port`) или URL DNS-over-HTTPS;
- `type` — `dns` или `doh`;
- `detour` — outbound сервера, пусто, если не задан.

| Метрика | Тип | Метки | Смысл |
|---|---|---|---|
| `keen_pbr_dns_proxy_queries_total` | counter | `proxy_port`, `transport` = `udp`, `tcp` | Запросы, полученные от dnsmasq, в том числе отброшенные. |
| `keen_pbr_dns_proxy_dropped_total` | counter | `proxy_port`, `transport`, `reason` = `overload`, `malformed` | Запросы, которые прокси не обслужил: не было свободного места (`overload`) или это не DNS-запрос (`malformed`). |
| `keen_pbr_dns_proxy_responses_total` | counter | `proxy_port`, `source` = `upstream`, `proxy_servfail` | Ответы клиентам: от сервера (`upstream`) или SERVFAIL самого прокси, когда ни один сервер не ответил (`proxy_servfail`). |
| `keen_pbr_dns_proxy_upstream_requests_total` | counter | `proxy_port`, `server`, `upstream`, `type`, `detour`, `result` | Попытки получить ответ от сервера по результатам: `ok`, `nxdomain`, `servfail`, `refused`, `other_rcode`, `timeout`, `connection_refused`, `unreachable`, `connection_closed`, `tls_error`, `http_error`, `bad_answer`, `not_resolved`, `network_error`. |
| `keen_pbr_dns_proxy_upstream_response_duration_seconds` | histogram | как выше, без `result` | Время до ответа сервера. Считаются только попытки, на которые пришёл ответ. Границы (с): 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, +Inf. |
| `keen_pbr_dns_proxy_upstream_up` | gauge | как выше, без `result` | `0` — прокси пропускает сервер после ошибки (или не может его использовать), `1` — иначе. |
| `keen_pbr_dns_proxy_upstream_consecutive_failures` | gauge | как выше, без `result` | Попытки подряд без ответа или с ответом SERVFAIL или REFUSED. Любой другой ответ обнуляет счётчик. При 10 появляется предупреждение «DNS-сервер не отвечает». |
| `keen_pbr_dns_proxy_inflight` | gauge | `transport` | Запросы, которые прокси обрабатывает сейчас. |

Доля неудачных попыток сервера за 5 минут:

```promql
sum by (server) (rate(keen_pbr_dns_proxy_upstream_requests_total{result=~"timeout|connection_refused|servfail|refused"}[5m]))
  / sum by (server) (rate(keen_pbr_dns_proxy_upstream_requests_total[5m]))
```

### Дашборд Grafana и алерты

Импортируйте [keen-pbr-dashboard.json](https://keen-pbr.fyi/grafana/keen-pbr-dashboard.json) в Grafana (Dashboards > New > Import). Нужен источник данных Prometheus с UID `prometheus`. В дашборде есть переменные `job` и `outbound`, обновление каждые 30 с и разделы Status, Outbounds, DNS interception, DNS proxy, Sets & lists и Errors. Интервал scrape должен быть не больше 30 с: столбчатые панели считают `increase()` по окнам в одну минуту.

Пример правил алертов:

```yaml
groups:
  - name: keen-pbr
    rules:
      - alert: KeenPbrDown
        expr: up{job="keen-pbr"} == 0
        for: 2m
      - alert: KeenPbrOutboundDown
        expr: keen_pbr_probe_up == 0
        for: 5m
        annotations:
          summary: "Проверка {{ $labels.outbound }} неуспешна ({{ $labels.test_outbound }})"
      - alert: KeenPbrProbeStale
        # Пробы перестали завершаться (интервал проб должен быть заметно меньше 30м).
        expr: increase(keen_pbr_probe_attempts_total[30m]) == 0
        for: 10m
      - alert: KeenPbrErrors
        expr: sum by (subsystem) (increase(keen_pbr_errors_total[10m])) > 0
        for: 5m
      - alert: KeenPbrDnsHoldSlow
        # 0.03 с — дедлайн удержания по умолчанию (intercept hold_timeout_ms: 30).
        expr: >
          histogram_quantile(0.99, sum by (le)
            (rate(keen_pbr_dns_hold_duration_seconds_bucket[5m]))) > 0.03
        for: 10m
      - alert: KeenPbrQueueOverruns
        expr: sum by (queue) (increase(keen_pbr_queue_overruns_total[10m])) > 0
      - alert: KeenPbrListStale
        expr: time() - keen_pbr_list_last_update_timestamp_seconds > 86400
        for: 30m
```

---

## POST /api/lists/refresh

Обновляет списки с удалёнными URL из активной конфигурации демона.

- Если `name` указан, обновляется только этот список.
- Если `name` не указан, обновляются все списки с URL.
- Если содержимое списков изменилось и это влияет на маршрутизацию или DNS, keen-pbr сразу применяет изменения.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/lists/refresh \
  -H "Content-Type: application/json" \
  -d '{"name":"apple"}'
```

Обновить все списки с URL:

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/lists/refresh
```

### Тело запроса (опционально)

```json
{
  "name": "apple"
}
```

- `name` *(опционально string)*: Имя списка для обновления.

### Ответ (200)

```json
{
  "status": "ok",
  "message": "Lists refreshed and runtime reloaded",
  "refreshed_lists": ["apple", "google"],
  "changed_lists": ["apple"],
  "failed_lists": [],
  "reloaded": true
}
```

Поля успешного ответа:

- `refreshed_lists` *(array[string])*: Списки с URL, которые были обновлены.
- `changed_lists` *(array[string])*: Обновлённые списки, содержимое которых изменилось.
- `failed_lists` *(array[string])*: Списки с URL, которые не удалось обновить.
- `reloaded` *(boolean)*: Были ли применены изменения, потому что изменённые списки используются.

### Коды статуса / ошибки

- `200`: Операция обновления завершена.
- `400`: Указанный список существует, но не имеет URL.
- `404`: Указанный список не найден.
- `409`: Обновление отклонено: есть несохранённый черновик конфигурации или уже выполняется другая операция.

Тело ответа об ошибке:

```json
{
  "error": "human-readable message"
}
```

---

## GET /api/config

Возвращает текущую конфигурацию и флаг наличия несохранённого черновика. Настройки `api` (пароль, CORS) в ответ не входят. Ответ также содержит состояние обновления списков.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/config
```

### Ответ

```json
{
  "config": {
    "daemon": { "pid_file": "/var/run/keen-pbr.pid", "cache_dir": "/var/cache/keen-pbr" },
    "outbounds": [],
    "lists": {},
    "route": {}
  },
  "is_draft": false,
  "list_refresh_state": {}
}
```

`is_draft` — `true`, если конфигурация передана через `POST /api/config`, но ещё не сохранена на диск.

### Ответ об ошибке (500)

```json
{
  "error": "Cannot open config file"
}
```

---

## POST /api/config

Проверяет переданный JSON как файл конфигурации и сохраняет его как черновик в памяти. На диск конфигурация не записывается, маршрутизация не меняется. Чтобы сохранить и применить черновик, вызовите `POST /api/config/save`.

Поле `api` в запросе игнорируется.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/config \
  -H "Content-Type: application/json" \
  -d @new-config.json
```

### Ответ

```json
{
  "status": "ok",
  "message": "Config staged in memory"
}
```

### Ответ об ошибке (400 — ошибка валидации)

```json
{
  "error": "Validation failed",
  "validation_errors": [
    { "path": "outbounds[0].interface", "message": "interface is required" }
  ]
}
```

---

## POST /api/config/save

Запускает в фоне сохранение черновика на диск и применение конфигурации. Ответ `202` означает только, что операция принята, а не что она завершена.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/config/save
```

### Принятая операция (202)

```json
{
  "operation_id": "op-123",
  "status": "accepted"
}
```

### Ответ об ошибке (400 — нет черновика)

```json
{
  "error": "No staged config to save"
}
```

Другие ошибки: `400` (черновик не прошёл проверку), `409` (уже выполняется другая операция), `500`.

После ответа `202` следите за полем `lifecycle_operation` в `GET /api/health/service` или подпишитесь на `/api/status/events`, пока операция не завершится.

Если применение завершилось ошибкой, а файл уже сохранён, в `GET /api/health/service` появится `rollback_available: true`. Файл при этом остаётся новым. Чтобы вернуть прежнюю конфигурацию, вызовите `POST /api/config/rollback` (ответ `409`, если откатывать нечего или идёт другая операция). `POST /api/config/discard` только отбрасывает несохранённый черновик и ничего не меняет в файле и работе маршрутизации.

---

## GET /api/runtime/outbounds

Возвращает текущее состояние outbounds: какой канал сейчас выбран в группе urltest или icmptest (группа сама выбирает канал с самой низкой задержкой), доступен ли интерфейс и не отключён ли он после серии сбоев.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/runtime/outbounds
```

### Ответ

```json
{
  "outbounds": [
    {
      "tag": "vpn",
      "type": "interface",
      "status": "healthy",
      "interfaces": [
        { "outbound_tag": "vpn", "interface_name": "tun0", "status": "active" }
      ]
    },
    {
      "tag": "auto_select",
      "type": "urltest",
      "status": "healthy",
      "interfaces": [
        { "outbound_tag": "vpn", "interface_name": "tun0", "status": "active" },
        { "outbound_tag": "vpn2", "interface_name": "tun1", "status": "backup" }
      ]
    }
  ]
}
```

- `type`: `interface`, `table`, `blackhole`, `ignore`, `urltest` или `icmptest`.
- `status` outbound: `healthy` (работает), `degraded` (работает частично), `unavailable` (нет доступного пути), `unknown`.
- `status` в `interfaces`: `active` (через него идёт трафик), `backup` (резерв), `degraded`, `unavailable` (отключён после серии сбоев), `unknown`. В группе urltest или icmptest выбранный канал имеет статус `active`.
- Могут быть дополнительные поля: `latency_ms`, `probe_target`, `detail` и счётчики пакетов.

---

## POST /api/dns/servers/check

Спрашивает все DNS-серверы из применённой конфигурации (не из черновика) о нескольких сайтах и показывает, какие серверы работают, не отвечают или подменяют ответы. Серверы типа `keenetic` проверяются по каждому адресу, который сообщает Keenetic. Одновременно идёт только одна проверка. Все поля тела необязательны.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/dns/servers/check \
  -H "Content-Type: application/json" \
  -d '{"domains": ["yandex.ru", "youtube.com"], "custom_domains": ["example.org"], "servers": ["google_dns"]}'
```

- `domains` — сайты, которые точно существуют: ответ «не существует» для них считается подозрением. По умолчанию `["yandex.ru"]`, если не задан `custom_domains`.
- `custom_domains` — ваши сайты: «не существует» и отсутствие адресов подозрением не считаются.
- `servers` — названия серверов (`tag`) из `dns.servers`. По умолчанию проверяются все.

Всего можно указать не больше 5 сайтов.

### Ответ

```json
{
  "overall": "warning",
  "servers": [
    {
      "tag": "google_dns",
      "type": "static",
      "endpoints": [
        {
          "address": "8.8.8.8",
          "status": "ok",
          "nxdomain_hijack": false,
          "results": [
            {
              "domain": "yandex.ru",
              "status": "ok",
              "severity": "ok",
              "rcode": 0,
              "addresses": ["77.88.55.60"],
              "duration_ms": 14.2
            },
            {
              "domain": "youtube.com",
              "status": "nxdomain",
              "severity": "warning",
              "rcode": 3,
              "addresses": [],
              "duration_ms": 9.8
            }
          ]
        }
      ]
    }
  ]
}
```

- `overall` — `ok`, `warning` (есть признаки подмены) или `error` (какой-то сервер не отвечает).
- `status` результата — `ok`, `nxdomain`, `nodata`, `stub_address`, `servfail`, `refused`, `rcode` либо причина, по которой ответа нет: `timeout`, `connection_refused`, `unreachable`, `connection_closed`, `tls_error`, `http_error`, `bad_answer`, `not_resolved`, `network_error`. `severity` — `ok`, `warning` или `error`.
- `nxdomain_hijack` — `true`, если сервер ответил адресами на несуществующее имя.
- Статус адреса `skipped` значит, что адрес не проверялся (например, IPv6 выключен), `unavailable` — что его нельзя проверить (например, DNS-over-HTTPS без сертификатов); причина в `reason`.

Ошибки: `400` — неверное тело запроса, больше 5 сайтов, неверное имя сайта или неизвестный `tag`; `409` — предыдущая проверка ещё идёт; `503` — сервис остановлен.

---

## POST /api/routing/test

Показывает, через какой outbound пойдёт трафик к домену или IP-адресу. Для домена сначала определяет его IP-адреса. Затем сверяет правила маршрутизации со списками (ожидаемый outbound) и наборами firewall (фактический outbound). Так можно найти расхождение, не перезапуская демон.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/routing/test \
  -H "Content-Type: application/json" \
  -d '{"target": "example.com", "proto": "tcp", "dest_port": 443}'
```

Необязательные поля пакета (`proto` — `tcp`, `udp` или `other`, `dest_port`,
`src_addr`, `src_port` и `dscp`) нужны, чтобы проверить правила с фильтрами по
портам, адресам и {{< term "DSCP" >}}. Без них проверяется только попадание цели в списки.

### Ответ

```json
{
  "target": "example.com",
  "is_domain": true,
  "resolved_ips": ["93.184.216.34"],
  "results": [
    {
      "ip": "93.184.216.34",
      "expected_outbound": "vpn",
      "actual_outbound": "vpn",
      "ok": true,
      "list_match": { "list": "my_domains", "via": "domain" }
    }
  ],
  "warnings": [],
  "rule_diagnostics": [],
  "no_matching_rule": false
}
```

- `ok` — ожидаемый и фактический outbound совпали. Вместо имени outbound может быть `(default)` (ни одно правило не подошло, трафик идёт по умолчанию) или `(unknown)` (определить не удалось).
- `warnings` — некритичные предупреждения, например об ошибке DNS. `rule_diagnostics` — разбор по каждому правилу (его показывает веб-интерфейс). `no_matching_rule` — `true`, если ни одно правило не подошло.

---

## GET /api/health/routing

Возвращает отчёт о том, правильно ли настроены маршруты и firewall: правила с метками пакетов (fwmark — число, по которому ядро выбирает маршрут), таблицы маршрутов и правила выбора таблицы. Отчёт обновляется в фоне, поэтому сразу после запуска он может ненадолго показать неполный результат.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/routing
```

### Ответ

```json
{
  "overall": "ok",
  "firewall_backend": "iptables",
  "firewall": {
    "chain_present": true,
    "prerouting_hook_present": true,
    "verification_state": "verified",
    "detail": "ok"
  },
  "firewall_rules": [
    {
      "set_name": "<direct>",
      "action": "mark",
      "expected_fwmark": "0x00010000",
      "actual_fwmark": "0x00010000",
      "status": "ok"
    }
  ],
  "route_tables": [
    {
      "table_id": 150,
      "outbound_tag": "vpn",
      "expected_interface": "tun0",
      "expected_gateway": "10.8.0.1",
      "table_exists": true,
      "default_route_present": true,
      "interface_matches": true,
      "gateway_matches": true,
      "status": "ok"
    }
  ],
  "policy_rules": [
    {
      "fwmark": "0x00010000",
      "fwmask": "0x00ff0000",
      "expected_table": 150,
      "priority": 1000,
      "rule_present_v4": true,
      "rule_present_v6": true,
      "status": "ok"
    }
  ]
}
```

`firewall.verification_state`: `verified` (проверено), `unavailable` (данные firewall недоступны), `failed` (проверка не пройдена). В `firewall.detail` при успехе значение `ok`, при проблеме — описание причины. Необязательный массив `warnings` содержит предупреждения (например, не настроен NAT или включён строгий `rp_filter`); на `overall` они не влияют.

**Общие значения статуса:**
- `ok` — все проверки пройдены
- `degraded` — одна или несколько проверок не пройдены
- `error` — исключение помешало завершению проверок

**Значения статуса проверки:**
- `ok` — проверка пройдена
- `missing` — ожидаемый элемент не найден в ядре
- `mismatch` — элемент найден, но конфигурация отличается

### Ответ об ошибке (500)

```json
{
  "overall": "error",
  "error": "failed to connect to netlink socket"
}
```

---

## GET /api/dns/test

Передаёт события перехвата как поток Server-Sent Events (сервер присылает события по мере появления). Первым приходит `HELLO`. Именно этот поток использует проверка DNS в веб-интерфейсе: она запрашивает специальный домен-маркер (`check.keen-pbr.fyi`) и ждёт его появления в потоке.

- `show=keen-pbr` (по умолчанию): только события с маркером. Поток закрывается, когда приходит нужный маркер. Параметр `domain=<маркер>` отделяет вашу проверку от чужих.
- `show=all` (или `show=full`): поток остаётся открытым и показывает DNS-запросы, TLS {{< term "SNI" >}}, HTTP Host, {{< term "QUIC" >}} Initial и маркеры, в том числе для доменов, которых нет в списках.

`show=all` показывает трафик всех клиентов, поэтому работает только при включённой аутентификации API. Без неё ответ `403`. Режим по умолчанию доступен всегда. Неверные `show` или `domain` дают `400`.

```bash {filename="bash"}
curl -N -u admin:password 'http://127.0.0.1:12121/api/dns/test?show=all'
```

### Пример потока

```text
data: {"type":"HELLO"}

data: {"type":"INTERCEPT","seq":42,"ts_ms":1712345678123,"source":"dns","client_ip":"192.168.1.10","domain":"example.com","lists":["streaming"],"ips":["203.0.113.7"],"added":1,"refreshed":0,"errors":0,"hold_us":180,"timed_out":false}
```
