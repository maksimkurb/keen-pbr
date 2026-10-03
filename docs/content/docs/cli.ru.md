---
title: Использование CLI keen-pbr
weight: 4
aliases:
  - /docs/advanced/cli/
  - /docs/advanced/signals/
---

`keen-pbr` умеет запускать сервис маршрутизации, проверять его состояние в реальном времени, скачивать данные списков и проверять решения маршрутизации.

## Использование

```text
Usage: keen-pbr [options] <command>

Options:
  --config <path>    Путь к JSON файлу конфигурации
  --log-level <lvl>  Уровень логов: error, warn, info, verbose, debug
  --no-api           Отключить REST API во время выполнения
  --use-raw-prerouting  Использовать raw PREROUTING для пересылаемого IPv4 (только iptables)
  --use-raw6-prerouting Использовать raw PREROUTING для пересылаемого IPv6 (только iptables)
  --version         Показать версию и выйти
  --help            Показать эту справку и выйти

Commands:
  service
  status
  download
  test-routing <ip-or-domain>
```

Файл конфигурации обычно `/etc/keen-pbr/config.json` на OpenWrt и Debian, и `/opt/etc/keen-pbr/config.json` на Keenetic / NetCraze.

## Опции

| Флаг | Описание |
|---|---|
| `--config <path>` | Путь к JSON файлу конфигурации. |
| `--log-level <lvl>` | Детализация логов: `error`, `warn`, `info`, `verbose` или `debug`. |
| `--log-target <target>` | Назначение логов: `stderr`, `syslog` или `both`. По умолчанию используется `syslog` для `service` и `stderr` для интерактивных команд. |
| `--no-api` | Отключить REST API, даже если он включён в конфиге. |
| `--use-raw-prerouting` | Использовать raw PREROUTING для классификации пересылаемого IPv4-трафика; доступно только с iptables. |
| `--use-raw6-prerouting` | Использовать raw PREROUTING для классификации пересылаемого IPv6-трафика; доступно только с iptables. |
| `--version` | Вывести версию и выйти. |
| `--help` | Вывести справку и выйти. |

### `--use-raw-prerouting`

Этот флаг переносит классификацию только пересылаемого IPv4-трафика из `mangle
PREROUTING` в `raw PREROUTING`. Локально сгенерированный трафик остаётся в
`mangle OUTPUT`, а IPv6 настраивается независимо флагом
`--use-raw6-prerouting`.

На Keenetic / NetCraze init-скрипт независимо проверяет и при
необходимости загружает `iptable_raw.ko` и `ip6table_raw.ko`. По умолчанию
`KEEN_PBR_RAW_PREROUTING="auto"`: каждая семья использует RAW только после
успешной проверки, иначе для неё используется mangle. Чтобы всегда
использовать mangle, добавьте в `/opt/etc/keen-pbr/defaults`:

```sh
KEEN_PBR_RAW_PREROUTING="disable"
```

Затем перезапустите сервис. Значение `auto` проверяет IPv4 и IPv6 независимо:
доступное семейство использует RAW, недоступное — mangle. Значения `enable`,
`ipv4-only` и `ipv6-only` требуют RAW для соответствующего семейства и не
допускают переход на mangle при ошибке проверки. RAW работает до conntrack и
намеренно не использует CONNMARK. Сравнение последствий для Keenetic приведено в
[инструкции по установке Keenetic / NetCraze]({{< relref "/docs/getting-started/installation/keenetic" >}}).

Проверить обе возможности RAW можно так:

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
| `status` | Показать состояние маршрутизации, таблиц маршрутизации, правил и верификации firewall, затем выйти. |
| `download` | Загрузить все списки с URL в кэш, затем выйти. |
| `test-routing <ip-or-domain>` | Сравнить ожидаемую и фактическую маршрутизацию для данного IP или домена. |

{{% details title="Удалённые команды: `generate-resolver-config`, `resolver-config-hash`" closed="true" %}}
Интеграция с dnsmasq удалена. Команды `generate-resolver-config <res>` и `resolver-config-hash` скрыты и оставлены как устаревшие заглушки на один релиз: они завершаются с кодом 0 и ничего не выводят (`generate-resolver-config` выводит список резервных upstream из пакета, если платформа его поставляет). Они нужны только для того, чтобы старая конфигурация dnsmasq со строкой `conf-script=... generate-resolver-config ...` продолжала запускаться, пока обновление пакета не очистит её.
{{% /details %}}

## Сигналы

Когда `keen-pbr` работает как управляемый сервис (демон), им также можно управлять через Unix-сигналы:

| Сигнал | Действие |
|---|---|
| `SIGUSR1` | Повторная проверка таблиц маршрутизации и немедленный запуск urltest-замеров задержки |
| `SIGHUP` | Полная перезагрузка: повторная загрузка списков при изменениях, повторное применение правил firewall и маршрутизации |
| `SIGTERM` / `SIGINT` | Корректное завершение работы |

Пример полной перезагрузки через сигнал SIGHUP:

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
Firewall backend: nftables

Outbounds:
  corp_vpn [interface] iface=corp_vpn fwmark=0x00010000 table=402
    route   table=402 default dev corp_vpn ............................... OK
    rule    0x00010000/0x00ff0000 -> table=402 pri=402 ........... OK [v4+v6]
  auto_vpn [urltest] fwmark=0x00060000 table=407
    route   table=407 default dev corp_vpn ............................... OK
    rule    0x00060000/0x00ff0000 -> table=407 pri=407 ........... OK [v4+v6]

Firewall:
  chain   KeenPbrTable / prerouting hook ............................... OK
    rule    kpbr4_generic -> MARK 0x00010000 ........................ MISSING
     правило не найдено в цепочке prerouting nftables

Overall: DEGRADED (2 проверки не пройдено)
Статус: OK / MISSING / MISMATCH / ERROR
```

Загрузить все списки с URL:

```bash {filename="bash"}
keen-pbr download
```

Пример вывода:

```text
[google] Not modified (304)
[internal.site] Skipped (no URL)
[generic] Skipped (no URL)
```

Протестировать совпадение ожидаемой и фактической маршрутизации:

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
