---
title: REST API
weight: 5
aliases:
  - /docs/advanced/rest-api/
---

Эта страница описывает встроенный HTTP REST API.

REST API доступен, если:
- Установлена полная версия пакета (`keen-pbr`, не `keen-pbr-headless`)
- В конфиге включён `api.enabled: true`
- При запуске не был передан флаг `--no-api`

## Конфигурация

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

Интерактивно сгенерировать хеш пароля:

```bash
keen-pbr hash-password
```

Чтобы включить аутентификацию и сразу записать новый хеш в выбранный
`config.json`, выполните `keen-pbr --config /path/to/config.json hash-password --update`.
Команда атомарно обновит файл и напомнит о необходимости перезапуска службы,
поскольку она изменяет файл вне работающего демона. Изменения аутентификации и
CORS, сделанные через веб-интерфейс, сохраняются и вступают в силу сразу, без
применения отложенного черновика конфигурации маршрутизации.

Параметр `device_name` необязателен и может быть пустым. Если он задан,
веб-интерфейс использует его в заголовке страницы браузера и под логотипом
keen-pbr, чтобы различать устройства.

Для хеширования пароля используется реализация Argon2id из Monocypher. Хеши
PBKDF2 из предыдущих версий не поддерживаются — их необходимо создать заново.
API конфигурации никогда не возвращает и не принимает сохранённый хеш. Веб-интерфейс
получает только состояние через `GET /api/auth/password`, а новый пароль в открытом
виде отправляет только в `POST /api/auth/settings`; хеширование выполняет демон.

Клиенты API могут использовать Basic-аутентификацию с именем пользователя
`admin` или Bearer-токен, полученный через `POST /api/auth/login`. Одновременно
действует только одна Bearer-сессия: новый успешный вход немедленно завершает
предыдущую сессию веб-интерфейса. Сессия истекает через 24 часа и теряется при
перезапуске демона.

Когда аутентификация отключена, межсайтовые запросы браузера запрещены. Когда
она включена, разрешены точно указанные источники, а также источники расширений
Chrome и Firefox. При входе и Basic-аутентификации пароль передаётся API,
поэтому используйте доверенную сеть или HTTPS reverse proxy.

По умолчанию API прослушивает `0.0.0.0:12121`. Все эндпоинты обслуживаются на настроенном адресе `api.listen`.

Полная схема запросов и ответов находится в [OpenAPI-документе](https://github.com/maksimkurb/keen-pbr/blob/main/docs/openapi.yaml). Основные маршруты:

| Маршрут | Назначение |
|---|---|
| `GET /api/health/service` | Состояние службы, черновика конфигурации, операции жизненного цикла и отката. |
| `POST /api/service/start`, `/stop`, `/restart` | Асинхронный запуск, остановка или перезапуск маршрутизации. |
| `GET /api/config`, `POST /api/config` | Чтение или подготовка проверенного черновика конфигурации. |
| `POST /api/config/discard`, `/save`, `/rollback` | Отмена черновика, сохранение и применение либо явный откат неудачного применения. |
| `GET /api/status/events` | Поток Server-Sent Events о состоянии службы, outbounds и интерфейсов. |
| `GET /api/runtime/outbounds`, `/api/runtime/interfaces` | Текущее состояние outbounds и список системных интерфейсов. |
| `GET /api/health/routing`, `POST /api/routing/test` | Кэшированная диагностика маршрутизации/firewall и проверка маршрута для цели. |
| `POST /api/lists/refresh` | Обновление одного или всех удалённых списков URL. |
| `GET /api/dns/test` | Поток событий перехвата DNS/L7 или проверка DNS-маркера веб-интерфейса. |
| `GET /api/diagnostics/command-failure` | Чтение недавнего журнала ошибок команд. |
| `GET /metrics` | Метрики Prometheus в текстовом формате. |
| `/api/auth/status`, `/api/auth/login`, `/api/auth/logout`, `/api/auth/password`, `/api/auth/settings` | Состояние аутентификации и управление сессией/паролем. |

---

## GET /api/health/service

Возвращает версию запущенного демона и состояние среды выполнения маршрутизации.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/service
```

### Ответ

```json
{
  "version": "3.0.0",
  "build": "20261010120000",
  "status": "running",
  "runtime_state": "running",
  "runtime_state_reason": "config apply complete",
  "os_type": "debian",
  "os_version": "12",
  "build_variant": "generic",
  "config_is_draft": false,
  "rollback_available": false
}
```

При включённом перехвате этот ответ также содержит объект `intercept`. В нём
раздельно указаны `dns_hold_active` и `l7_active`, возможности
(`nfqueue`, `nflog`, `connbytes`) и счётчики DNS-разбора/hold, частичных TCP,
L7-пакетов, обновлений наборов, очистки conntrack и переполнений NFQUEUE/NFLOG.
Недоступность NFQUEUE не означает недоступность L7 и наоборот.

Для текущего состояния outbounds во время выполнения (здоровье, задержка, circuit breaker) используйте `GET /api/runtime/outbounds`.

## GET /metrics

Возвращает текстовый формат Prometheus 0.0.4. Эндпоинт использует ту же
аутентификацию, что и API. При недоступности поставщика метрик возможен ответ
`503`:

```bash
curl -u admin:password http://127.0.0.1:12121/metrics
```

Все длительности — числа с плавающей точкой в **секундах**. Серия, не имеющая
смысла, **не публикуется** (а не выводится как `0`). `# HELP` и `# TYPE`
выводятся один раз на семейство, пустые семейства не печатаются. Это
несовместимое изменение: прежние `keen_pbr_intercept_*`,
`keen_pbr_dns_write_duration_seconds`, метрики времени netlink, гистограмма
проб и `probe_success_ratio` удалены.

### Процесс и конфигурация

| Метрика | Тип | Метки | Смысл |
|---|---|---|---|
| `keen_pbr_build_info` | gauge | `version`, `commit`, `firewall_backend` | Всегда `1`; сведения о сборке. |
| `keen_pbr_process_start_time_seconds` | gauge | - | Unix-время старта; uptime: `time() - keen_pbr_process_start_time_seconds`. |
| `keen_pbr_active_rules` | gauge | - | Применённые правила firewall. |
| `keen_pbr_config_reload_last_success_timestamp_seconds` | gauge | - | Unix-время, когда рантайм последний раз завершил применение конфигурации (старт тоже считается). До первого применения не публикуется. |
| `keen_pbr_config_reload_errors_total` | counter | - | Неудачные перезагрузки и применения конфигурации. |
| `keen_pbr_list_last_update_timestamp_seconds` | gauge | `list` | Unix-время последней успешной проверки удалённого списка (независимо от изменения содержимого). Хранится в памяти: до первого успешного обновления в этом процессе не публикуется. |
| `keen_pbr_list_update_errors_total` | counter | `list` | Неудачные обновления удалённого списка. |

### Ошибки

`keen_pbr_errors_total{subsystem}` — одно семейство счётчиков, все значения
выводятся всегда (включая нули): `firewall_apply`, `netlink`, `set_write`
(запись в динамические наборы), `conntrack`, `dns_parse`, `dns_tcp_partial`
(не полностью собранные DNS-сообщения по TCP) и `dns_late_write` (неудачные
отложенные DNS-записи).

### Пробы (urltest / icmptest)

Метки: `outbound` (дочерний), `test_outbound` (группа), `interface`, `type`
(`urltest` или `icmptest`).

| Метрика | Тип | Смысл |
|---|---|---|
| `keen_pbr_probe_attempts_total` | counter | Принятые результаты проб. |
| `keen_pbr_probe_successes_total` | counter | Успешные результаты проб. |
| `keen_pbr_probe_packets_sent_total` | counter | Отправленные ICMP echo-запросы. Только `type="icmptest"`. |
| `keen_pbr_probe_packets_received_total` | counter | Полученные ICMP echo-ответы. Только `type="icmptest"`. |
| `keen_pbr_probe_up` | gauge | `1` — последняя проба успешна, `0` — неуспешна. |
| `keen_pbr_probe_last_success_timestamp_seconds` | gauge | Unix-время последней успешной пробы. |
| `keen_pbr_probe_latency_seconds` | gauge | Задержка последней успешной пробы: для ICMP — среднее по полученным ответам, для URL — время запроса. Точность лучше миллисекунды. |
| `keen_pbr_probe_latency_min_seconds`, `keen_pbr_probe_latency_max_seconds` | gauge | Самый быстрый и самый медленный ответ последней успешной ICMP-пробы. Только `type="icmptest"`. |
| `keen_pbr_urltest_selected` | gauge | Метки `group`, `outbound`: `1` у выбранного сейчас outbound группы, `0` у остальных. |
| `keen_pbr_urltest_selection_changes_total` | counter | Метка `group`: число смен выбора с момента регистрации группы (применение конфигурации регистрирует её заново и сбрасывает счётчик). |

#### Когда серии не публикуются

- `probe_up` — пока не завершилась первая проба.
- `probe_latency_seconds`, `probe_latency_min_seconds`, `probe_latency_max_seconds` —
  если **последняя** проба неуспешна или проб ещё не было. В Grafana это разрыв
  линии, а не `0`.
- `probe_last_success_timestamp_seconds` — до первой успешной пробы.
- `list_last_update_timestamp_seconds` и
  `config_reload_last_success_timestamp_seconds` — до первого успеха.

### Interception

Счётчики обновляются на путях DNS/L7 одним relaxed-атомарным инкрементом
заранее выделенного счётчика; значения меток — фиксированные слоты массива и
превращаются в текст только при запросе `/metrics`.

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
| `keen_pbr_conntrack_requests_total` | counter | - | Запросы очистки conntrack. |
| `keen_pbr_conntrack_deleted_total` | counter | - | Удалённые записи conntrack. |

Доля «медленных» записей вычисляется по bucket `le="0.025"` метрики
`keen_pbr_set_write_duration_seconds` (записи дольше 25 мс).

Не экспортируются: байты/пакеты по outbound и число элементов в наборах.
Для обоих пришлось бы читать счётчики firewall или netlink-дамп наборов при
каждом scrape (через подпроцесс или дамп ядра, по-разному для каждого бэкенда
firewall); демон намеренно этого не делает.

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

### Дашборд Grafana и алерты

Импортируйте [keen-pbr-dashboard.json](https://keen-pbr.fyi/grafana/keen-pbr-dashboard.json)
в Grafana (Dashboards - New - Import). Нужен источник данных Prometheus с UID
`prometheus`; в дашборде переменные `job` и `outbound`, обновление каждые 30 с
и разделы Status, Outbounds, DNS interception, Sets & lists и Errors. Интервал
scrape должен быть не больше 30 с: столбчатые панели считают `increase()` по
окнам в одну минуту.

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
          summary: "Проба {{ $labels.outbound }} неуспешна ({{ $labels.test_outbound }})"
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
- Если обновлённые данные изменились и затрагивают активную маршрутизацию/DNS, keen-pbr перестраивает состояние выполнения, чтобы изменения вступили в силу немедленно.

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
- `reloaded` *(boolean)*: Была ли перестроена среда выполнения маршрутизации, потому что изменённые списки использовались.

### Коды статуса / ошибки

- `200`: Операция обновления завершена.
- `400`: Указанный список существует, но не имеет URL.
- `404`: Указанный список не найден.
- `409`: Обновление отклонено, потому что есть незафиксированный черновик или уже находится в процессе другая операция конфигурации/выполнения.

Тело ответа об ошибке:

```json
{
  "error": "human-readable message"
}
```

---

## GET /api/config

Возвращает видимую конфигурацию и флаг наличия подготовленного черновика в памяти. Секретные настройки `api` намеренно опущены. Ответ также содержит состояние обновления списков.

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

`is_draft` — `true`, если конфигурация была подготовлена через `POST /api/config`, но ещё не сохранена на диск.

### Ответ об ошибке (500)

```json
{
  "error": "Cannot open config file"
}
```

---

## POST /api/config

Проверяет предоставленное JSON-тело как файл конфигурации и откладывает его в память. Конфигурация **НЕ** записывается на диск и среда выполнения маршрутизации **НЕ** изменяется. Используйте `POST /api/config/save` для сохранения и применения отложенного черновика.

Поле `api` в запросе игнорируется: демон сохраняет действующие настройки безопасности.

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

Принимает асинхронную операцию, которая сохраняет отложенную конфигурацию на диск, а затем согласует среду выполнения маршрутизации. HTTP `202` подтверждает только принятие операции, но не её завершение.

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

### Ответ об ошибке (400 — нет отложенной конфигурации)

```json
{
  "error": "No staged config to save"
}
```

После принятия операции проверяйте `lifecycle_operation` в `GET /api/health/service` или подпишитесь на `/api/status/events`, пока операция не завершится. Конфигурация сохраняется до согласования runtime. Если согласование завершается ошибкой после сохранения файла, демон сообщает о неисправном runtime и устанавливает `rollback_available`; прежний файл и runtime автоматически не восстанавливаются. `POST /api/config/rollback` явно восстанавливает сохранённую копию прежней конфигурации и согласует её с runtime. При успехе runtime становится активным, а соответствующий черновик удаляется. `POST /api/config/discard` только отменяет несохранённый черновик в памяти и не меняет файл или runtime.

---

## GET /api/runtime/outbounds

Возвращает текущее состояние outbounds демона во время выполнения: живой выбор urltest, достижимость интерфейса и статус circuit breaker.

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
        { "name": "tun0", "status": "up" }
      ]
    },
    {
      "tag": "auto_select",
      "type": "urltest",
      "status": "healthy",
      "selected_outbound": "vpn"
    }
  ]
}
```

---

## POST /api/routing/test

Разрешает цель (если это домен), сканирует настроенные правила маршрутизации по данным списков в кэше, чтобы определить ожидаемый outbound, и запрашивает живые наборы firewall ядра, чтобы определить фактический outbound. Полезно для диагностики несоответствий маршрутизации без перезапуска демона.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/routing/test \
  -H "Content-Type: application/json" \
  -d '{"target": "example.com", "proto": "tcp", "dest_port": 443}'
```

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
  ]
}
```

---

## GET /api/health/routing

Возвращает кэшированный канонический отчёт о состоянии маршрутизации и firewall ядра. При холодном или устаревшем снимке обновление запускается вне event loop, поэтому временно может отображаться ожидающий или деградированный результат; каждый запрос не выполняет синхронную проверку ядра.

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
    "detail": "chain KeenPbrTable found in table mangle"
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

Транслирует события перехвата как Server-Sent Events. Соединение получает
`HELLO`. По умолчанию `show=keen-pbr` выдаёт только marker-события и закрывается
после подходящего marker; `show=all` (или совместимый псевдоним `show=full`)
остаётся открытым и выдаёт наблюдения DNS, TLS SNI, HTTP Host, QUIC Initial и
marker, включая домены без совпадения со списками. Старые события `DNS` от
удалённого `dns.dns_test_server` больше не выдаются. Параметр
`domain=<сгенерированный marker>` в представлении по умолчанию изолирует
проверки друг от друга.

```bash {filename="bash"}
curl -N 'http://127.0.0.1:12121/api/dns/test?show=all'
```

### Пример потока

```text
data: {"type":"HELLO"}

data: {"type":"INTERCEPT","seq":42,"ts_ms":1712345678123,"source":"dns","client_ip":"192.168.1.10","domain":"example.com","lists":["streaming"],"ips":["203.0.113.7"],"added":1,"refreshed":0,"errors":0,"hold_us":180,"timed_out":false}
```
