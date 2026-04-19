# REST API

Эта страница описывает встроенный HTTP REST API.

REST API доступен, если:
- Установлена полная версия пакета (`keen-pbr`, не `keen-pbr-headless`)
- В конфиге включён `api.enabled: true`
- При запуске не был передан флаг `--no-api`

## Конфигурация

```json { filename="config.json" }
{
  "api": {
    "enabled": true,
    "listen": "0.0.0.0:12121"
  }
}
```

По умолчанию API прослушивает `0.0.0.0:12121`. Все эндпоинты обслуживаются на настроенном адресе `api.listen`.

---

## GET /api/health/service

Возвращает версию запущенного демона, состояние среды выполнения маршрутизации и сводку по конфигурации резолвера.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/service
```

### Ответ

```json
{
  "version": "3.0.0",
  "status": "running",
  "resolver_config_hash": "a3f7c1d9e2b84560abcdef1234567890",
  "resolver_config_hash_actual": "a3f7c1d9e2b84560abcdef1234567890",
  "config_is_draft": false
}
```

`resolver_config_hash` — это MD5-хеш ожидаемого сопоставления домен-ipset, полученного из текущей конфигурации. `resolver_config_hash_actual` отражает хеш конфигурации, которая была последний раз применена к работающему системному резолверу. Когда эти два значения различаются, конфигурация dnsmasq может быть устаревшей.

Для текущего состояния outbounds во время выполнения (здоровье, задержка, circuit breaker) используйте `GET /api/runtime/outbounds`.

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
  "reloaded": true
}
```

Поля успешного ответа:

- `refreshed_lists` *(array[string])*: Списки с URL, которые были обновлены.
- `changed_lists` *(array[string])*: Обновлённые списки, содержимое которых изменилось.
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

Возвращает текущую конфигурацию и флаг, указывающий, существует ли отложенный черновик в памяти.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/config
```

### Ответ

```json
{
  "config": {
    "daemon": { "pid_file": "/var/run/keen-pbr.pid", "cache_dir": "/var/cache/keen-pbr" },
    "api": { "enabled": true, "listen": "127.0.0.1:12121" },
    "outbounds": [],
    "lists": {},
    "route": {}
  },
  "is_draft": false
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
    { "path": "outbounds.vpn.interface", "message": "interface is required" }
  ]
}
```

---

## POST /api/config/save

Сохраняет отложенную конфигурацию на диск, затем применяет её к среде выполнения маршрутизации.

```bash {filename="bash"}
curl -X POST http://127.0.0.1:12121/api/config/save
```

### Ответ

```json
{
  "status": "ok",
  "message": "Config saved and applied",
  "saved": true,
  "applied": true,
  "rolled_back": false
}
```

### Ответ об ошибке (400 — нет отложенной конфигурации)

```json
{
  "error": "No staged config to save",
  "saved": false,
  "applied": false,
  "rolled_back": false
}
```

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
  -d '{"target": "example.com"}
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

Проверяет текущее состояние маршрутизации и firewall ядра против ожидаемой конфигурации: проверяет, что цепочка firewall существует, все правила на месте, ip table заполнены и ip rule установлены.

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/routing
```

### Ответ

```json
{
  "overall": "ok",
  "firewall_backend": "nftables",
  "firewall": {
    "chain_present": true,
    "prerouting_hook_present": true,
    "detail": "chain keen-pbr found in table mangle"
  },
  "firewall_rules": [
    {
      "set_name": "keen-pbr-my_domains",
      "action": "MARK",
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

Транслирует DNS-запросы, наблюдаемые встроенным listener `dns.dns_test_server` как Server-Sent Events. Каждый event-пayload — это JSON-объект. Соединение получает event `HELLO` немедленно, затем по одному event `DNS` на запрошенное имя, пока соединение открыто.

```bash {filename="bash"}
curl -N http://127.0.0.1:12121/api/dns/test
```

### Пример потока

```text
data: {"type":"HELLO"}

data: {"type":"DNS","domain":"example.com","source_ip":"192.168.1.10","ecs":"203.0.113.0/24"}

data: {"type":"DNS","domain":"connectivity-check.local","source_ip":"192.168.1.11","ecs":null}
```

