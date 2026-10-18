---
title: Использование CLI keen-pbr
weight: 4
aliases:
  - /docs/advanced/cli/
  - /docs/advanced/signals/
---

`keen-pbr` умеет запускать сервис маршрутизации, проверять его состояние в реальном времени, скачивать данные списков и проверять решения маршрутизации.

Команды `status`, `download` и `test-routing` обращаются к работающему сервису. Если сервис не запущен, они завершаются с ошибкой и кодом 1. Сначала запустите сервис (на роутере он стартует вместе с системой).

## Использование

```text
Usage: keen-pbr [options] <command>

Options:
  --config <path>    Path to JSON config file (default: <KEEN_PBR_DEFAULT_CONFIG_PATH>)
  --log-level <lvl>  Log level: error, warn, info, verbose, debug (default: info)
  --log-target <target>  Log destination: stderr, syslog, or both (default: both)
  --pid-file <path>  Override daemon.pid_file when running the service command
  --crash-report <path>  Last-crash report path (default: /tmp/keen-pbr-crash.log)
  --no-api           Disable REST API at runtime
  --use-raw-prerouting  Use raw PREROUTING for IPv4 forwarded traffic (iptables only)
  --use-raw6-prerouting Use raw PREROUTING for IPv6 forwarded traffic (iptables only)
  --version          Show version and exit
  --help             Show this help and exit

Commands:
  service                            Start the routing service (foreground)
  status                             Show routing/firewall status and exit
  download                           Download all configured lists to cache and exit
  test-routing <ip-or-domain> [packet options]
                                     Test expected vs actual routing for an IP or domain
      --proto <tcp|udp|other>        Packet protocol
      --src-addr <ip>                Packet source address
      --src-port <port>              Packet source port
      --dest-port <port>             Packet destination port
      --dscp <0-63>                  Packet DSCP (default: 0)
  resolver dnsmasq print-config      Print the dnsmasq config generated from dns.rules/dns.fallback (cached lists only; used as dnsmasq conf-script, always exits 0)
  resolver dnsmasq detect [--json]   List dnsmasq instances, their DNS endpoints and which one keen-pbr uses
  hash-password [--update]           Generate an authentication password hash; --update writes config.json
```

Файл конфигурации обычно `/etc/keen-pbr/config.json` на OpenWrt и Debian, и `/opt/etc/keen-pbr/config.json` на Keenetic / NetCraze.

## Опции

| Флаг | Описание |
|---|---|
| `--config <path>` | Путь к JSON файлу конфигурации. Работает только с командой `service`. |
| `--log-level <lvl>` | Детализация логов: `error`, `warn`, `info`, `verbose` или `debug`. |
| `--log-target <target>` | Назначение логов: `stderr`, `syslog` или `both`. По умолчанию для всех команд используется `both`. Пакетные сервисные файлы (systemd, OpenWrt procd, init-скрипт Keenetic) передают `--log-target syslog`, чтобы логи не дублировались, когда stdout/stderr дополнительно перехватываются системой инициализации. |
| `--pid-file <path>` | Путь к PID-файлу (файлу с номером процесса сервиса). Заменяет `daemon.pid_file` из конфига; только для `service`. |
| `--crash-report <path>` | Куда записать отчёт о последнем аварийном завершении. По умолчанию `/tmp/keen-pbr-crash.log`. |
| `--no-api` | Отключить REST API, даже если он включён в конфиге. |
| `--use-raw-prerouting` | {{< only iptables >}} Использовать raw {{< term "PREROUTING" >}} для классификации пересылаемого IPv4-трафика. |
| `--use-raw6-prerouting` | {{< only iptables >}} Использовать raw PREROUTING для классификации пересылаемого IPv6-трафика. |
| `--version` | Вывести версию и выйти. |
| `--help` | Вывести справку и выйти. |

### `--use-raw-prerouting`

Флаг нужен в основном на Keenetic / NetCraze. Прошивка время от времени пересобирает таблицу firewall `mangle`, и правила keen-pbr ненадолго пропадают. С флагом keen-pbr ставит правила для проходящего через роутер IPv4-трафика в таблицу `raw`, которую прошивка не пересобирает. Для IPv6 есть отдельный флаг `--use-raw6-prerouting`.

На Keenetic / NetCraze флаги выставляет скрипт запуска. Режим задаётся переменной `KEEN_PBR_RAW_PREROUTING` в `/opt/etc/keen-pbr/defaults`:

| Значение | Что делает |
|---|---|
| `auto` (по умолчанию) | Для IPv4 и IPv6 отдельно проверяет, работает ли `raw`, и использует его, если работает, иначе `mangle` |
| `disable` | Всегда использовать `mangle` |
| `enable` | Требовать `raw` для IPv4 и IPv6. Если `raw` недоступен, сервис не запустится |
| `ipv4-only`, `ipv6-only` | Требовать `raw` только для одного семейства адресов |

После изменения перезапустите сервис. Чем отличаются режимы на практике, описано в [инструкции по установке Keenetic / NetCraze]({{< relref "/docs/getting-started/installation/keenetic" >}}).

Проверить, доступен ли `raw`:

```sh
ls -l "/lib/modules/$(uname -r)/iptable_raw.ko"
ls -l "/lib/modules/$(uname -r)/ip6table_raw.ko"
grep -x raw /proc/net/ip_tables_names
grep -x raw /proc/net/ip6_tables_names
iptables -t raw -S
ip6tables -t raw -S
```

## Команды

| Команда | Описание |
|---|---|
| `service` | Запустить сервис маршрутизации на переднем плане. |
| `status` | Показать состояние службы, outbound, маршрутизации, firewall и проверок ядра, затем список проблем, как на главной странице веб-интерфейса, и выйти. Код выхода 1, если какая-то проверка не прошла или есть проблема уровня «ошибка». |
| `download [--reload]` | Загрузить все списки с URL в кэш и выйти. С `--reload` сервис сразу применяет списки, которые изменились. |
| `resolver dnsmasq print-config` | Вывести конфигурацию dnsmasq для отдельных DNS-серверов списков. |
| `hash-password [--update]` | Создать хеш пароля для входа в веб-интерфейс и API. |
| `resolver dnsmasq detect [--json]` | Показать экземпляры dnsmasq, их DNS-адреса и экземпляр, который использует keen-pbr. |
| `test-routing <ip-or-domain> [параметры пакета]` | Сравнить ожидаемую и фактическую маршрутизацию для данного IP или домена. Проверку выполняет служба, так же как проверку маршрутизации в веб-интерфейсе. |

### `resolver dnsmasq print-config`

Когда включена `dns.resolver_integration: "dnsmasq"`, эта команда печатает конфигурацию dnsmasq, которую собрал keen-pbr. Списки берутся только из кэша, ничего не скачивается. В ней соответствия «домен → сервер», исключения защиты от {{< term "DNS rebinding" >}} и резервные (`fallback`) серверы.

**Использование:**

```bash
keen-pbr resolver dnsmasq print-config
```

**Вывод:** Директивы конфигурации dnsmasq.

```text
# keen-pbr generated, do not edit

address=/use-application-dns.net/

no-resolv
# direct -> [DNS] 192.168.1.1
server=192.168.1.1
# dns proxy :52100 -> [DoH] https://dns.google/dns-query
server=127.0.0.89#52100

# List: ai_services

# direct -> [DNS] 1.1.1.1
server=/openai.com/www.openai.com/.../1.1.1.1
rebind-domain-ok=/openai.com/www.openai.com/...

# List: blocked

# dns proxy :52101 -> [DoH] https://dns.google/dns-query via vpn, [DoH] https://cloudflare-dns.com/dns-query
server=/example.com/.../127.0.0.89#52101
```

**Поведение:**
- Всегда завершается с кодом 0, даже при ошибках; ошибки печатаются строками-комментариями `# keen-pbr: ...`.
- Когда `resolver_integration: "none"` (по умолчанию) или конфиг не читается, в выводе только строки `server=` из `dnsmasq-fallback.conf`; динамические наборы заполняются перехватом.
- При `"dnsmasq"` и без серверов `fallback` эти строки `server=` добавляются в вывод.
- dnsmasq читает конфиг через эту команду (см. `conf-script=keen-pbr generate-resolver-config dnsmasq` в {{< term "drop-in" >}}).

### `hash-password`

Создаёт хеш пароля для входа в веб-интерфейс и API. Команда дважды спрашивает пароль.

```bash
keen-pbr hash-password
keen-pbr hash-password --update
```

Без `--update` команда печатает `Password hash: <хеш>`, который вы вставляете в `api.authentication.password_hash` сами. С `--update` она записывает в `config.json` `api.enabled: true`, `api.authentication.enabled: true` и хеш. После этого перезапустите сервис: `Restart the keen-pbr service for authentication changes to take effect.`

### `resolver dnsmasq detect` {#resolver-dnsmasq-detect}

Просмотр интеграции с dnsmasq без демона. Команде не нужен управляющий сокет.

```bash
keen-pbr resolver dnsmasq detect [--json]
```

Команда выводит все экземпляры dnsmasq, о которых сообщает hook-скрипт платформы: запущен ли, DNS-адрес и чем он подтверждён, слушающие сокеты, установлен ли {{< term "drop-in" >}} keen-pbr и какую конфигурацию загрузил dnsmasq, а также проблемы. В последней строке указано, какой экземпляр выбрал keen-pbr и как (`explicit`, `adopted` или `automatic`), либо почему выбрать не удалось.

```text
dnsmasq hook: /usr/lib/keen-pbr/dnsmasq-hook.sh (platform openwrt, binary /usr/sbin/dnsmasq)

  uci:cfg01411c  [selected: adopted]
    running:     yes (pid 1842)
    enabled:     yes
    endpoint:    127.0.0.1:53 (stamp_verified)
    listeners:   udp+tcp 0.0.0.0:53, udp+tcp [::]:53
    keen-pbr:    installed, loaded hash 3f2a9c0e1b7d4a55c6e8f0a1b2c3d4e5
    conf-dir:    /tmp/dnsmasq.cfg01411c.d (included)
    problem:     none

Selection: uci:cfg01411c (adopted)
```

- `--json` печатает те же данные одним JSON-объектом (`selected`, `selection`, `error_code`, `message`, `warnings`, `instances[]`).
- Код возврата `0`, если экземпляр выбран и проблем нет, иначе `1`.
- Если выбор неоднозначен, в конце выводится подсказка задать `dns.dnsmasq_instance`.

## Сигналы

Запущенным сервисом `keen-pbr` можно управлять сигналами:

| Сигнал | Действие |
|---|---|
| `SIGUSR1` | Заново собрать маршруты и правила firewall |
| `SIGHUP` | Перечитать `config.json` и применить его заново. В сборке `keen-pbr-headless` не работает |
| `SIGTERM` / `SIGINT` | Корректное завершение работы |

Пример: перечитать конфиг сигналом SIGHUP:

```bash {filename="bash"}
kill -HUP $(cat /var/run/keen-pbr.pid)
```

## Примеры

Проверить состояние маршрутизации и firewall:

```bash {filename="bash"}
keen-pbr status
```

Пример вывода:

```text
keen-pbr status - config: /etc/keen-pbr/config.json
Service: running (runtime running: startup complete) - keen-pbr 3.4.0 (build 20261012064940)
Firewall backend: nftables

Outbounds:
  corp_vpn [interface] iface=corp_vpn fwmark=0x00010000 table=402
    state ........................................................... HEALTHY
    route   table=402 default dev corp_vpn ............................... OK
    rule    0x00010000/0x00ff0000 -> table=402 pri=402 ........... OK [v4+v6]
  auto_vpn [urltest] fwmark=0x00060000 table=402
    state ........................................................... HEALTHY
    member  corp_vpn (corp_vpn) 41 ms ................................ ACTIVE
    member  backup_vpn (wg1) ....................................... DEGRADED
      probe failed: timeout
    route   table=402 default dev corp_vpn ............................... OK
    rule    0x00060000/0x00ff0000 -> table=402 pri=407 ........... OK [v4+v6]

Firewall:
  chain   KeenPbrTable / prerouting hook ............................... OK
  rule    kpbr4_generic -> MARK 0x00010000 ........................ MISSING
    rule not found in nftables prerouting chain
  rule    kpbr4_auto -> MARK 0x00060000 selected=corp_vpn .............. OK

Kernel 6.12.94:
  capability nfqueue ................................................... OK
  probe      set_write ................................................. OK
  probe      conntrack ................................................. OK

Interception: running, DNS hold active, L7 active
dnsmasq: ok, alive, 127.0.0.1:53 (uci:cfg01411c), 3 rules, 16 domains

Issues:
  ERROR  Firewall rules do not match: 1
         kpbr4_generic: rule not found in nftables prerouting chain
  WARN   auto_vpn: failing members: 1
         backup_vpn · degraded · probe failed: timeout

Overall: DEGRADED (1 check(s) failed, 1 error(s), 1 warning(s))
Status values: OK / MISSING / MISMATCH / ERROR
```

Загрузить все списки с URL:

```bash {filename="bash"}
keen-pbr download
```

Команда печатает ответ сервиса одной строкой JSON. В `refreshed_lists` перечислены обновлённые списки, в `changed_lists` те, у которых изменилось содержимое:

```text
{"ok":true,"protocol_version":2,"request_id":"cli-download","result":{"changed_lists":["google"],"failed_lists":[],"refreshed_lists":["google"],"reloaded":false}}
```

Если какой-то список не скачался, команда напишет ошибку и завершится с кодом 1.

Сравнить ожидаемую и фактическую маршрутизацию:

```bash {filename="bash"}
keen-pbr test-routing google.com
```

Пример вывода:

```text
Target: google.com
Resolved IPs: 2001:4860:4860::8888, 142.250.74.14

IP                        | List Match               | Expected Outbound  | Actual Outbound    | Status
---------------------------------------------------------------------------------------------------
2001:4860:4860::8888      | google (via google.com)  | corp_vpn           | corp_vpn           | OK
142.250.74.14             | google (via google.com)  | corp_vpn           | corp_vpn           | OK
```

По умолчанию проверяется только попадание в списки. Чтобы учесть и правила с портами, протоколом, адресом источника или DSCP, опишите пакет:

| Флаг | Описание |
|---|---|
| `--proto <tcp\|udp\|other>` | Протокол пакета. |
| `--src-addr <ip>` | Адрес клиента, который отправляет пакет. |
| `--src-port <port>` | Порт источника. |
| `--dest-port <port>` | Порт назначения. |
| `--dscp <0-63>` | Значение DSCP. Без `--dscp` пакет считается с DSCP 0. |

```bash {filename="bash"}
keen-pbr test-routing google.com --proto tcp --dest-port 443 --src-addr 192.168.1.10
```

Если правило зависит от поля, которое вы не указали (например, `src_addr`), для этого IP будет `(unknown)`.
