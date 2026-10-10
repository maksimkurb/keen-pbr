---
title: DNS
weight: 4
---

Маршрутизация по доменам работает и без настроек DNS: keen-pbr сам перехватывает
DNS-ответы и TLS/HTTP/QUIC-трафик и заполняет наборы ваших списков. См.
[архитектуру DNS-перехвата](https://github.com/maksimkurb/keen-pbr/blob/main/docs/dns-interception.md).
keen-pbr не настраивает, не перезапускает и не требует DNS-резолвер: dnsmasq,
DNS-прокси Keenetic, unbound и любой другой продолжают работать так, как вы их настроили.

Используйте настройки DNS для определения адресов серверов и при необходимости маршрутизации трафика,
основанной на доменах (из определённых списков) через выбранный DNS-сервер, обычно тот же VPN,
что будет переносить соответствующий трафик.

## Конфигурация

```json { filename="config.json" }
{
  "dns": {
    "servers": [...]
  }
}
```

| Поле | Тип | Обязательное | Описание |
|---|---|---|---|
| `servers` | array | нет | Определения DNS-серверов |
| `resolver_integration` | string | нет | `"none"` или `"dnsmasq"`; если опущено, `"dnsmasq"` используется если `rules` не пусто, иначе `"none"`. |
| `dnsmasq_instance` | string | нет | Селектор управляемого экземпляра dnsmasq: `uci:<секция>` (OpenWrt), `entware:default` (Keenetic), `systemd:<юнит>.service` (Debian). Пусто или не задано: автоматический выбор; нужен только если найдено несколько экземпляров. |
| `rules` | array | нет | Правила маршрутизации списков на DNS-серверы (активны только при `resolver_integration: "dnsmasq"`). |
| `fallback` | array of string | нет | Теги upstream DNS-серверов для запросов, не совпадших с правилом (активны только при `resolver_integration: "dnsmasq"`). |
| `dns_test_server` | object | нет | Устаревшее поле, принимается, но игнорируется; используйте `intercept.dns.marker` |

## Per-list DNS servers (dnsmasq) — Маршрутизация доменов через разные DNS

Когда `resolver_integration` установлен на `"dnsmasq"`, keen-pbr может управлять dnsmasq
и разрешать домены из определённых списков через разные DNS-серверы. Это полезно, когда вам
нужны CDN-ответы, соответствующие определённому VPN-региону, или разные upstream-серверы
для разных сервисов.

**Пример использования**: разрешать домены AI-сервисов через DNS-сервер, доступный через VPN,
чтобы CDN-ответы соответствовали регионе выхода VPN (иначе может произойти блокировка аккаунта).

### Пример конфигурации

```json { filename="config.json" }
{
  "dns": {
    "resolver_integration": "dnsmasq",
    "servers": [
      {"tag": "vpn_dns", "address": "1.1.1.1", "detour": "vpn"},
      {"tag": "isp_dns", "address": "192.168.1.1"}
    ],
    "rules": [
      {
        "list": ["ai_services"],
        "server": "vpn_dns",
        "allow_domain_rebinding": false
      }
    ],
    "fallback": ["isp_dns"]
  }
}
```

### Справочник полей

| Поле | Тип | Обязательное | Описание |
|---|---|---|---|
| `resolver_integration` | string | да | Установите `"dnsmasq"` для включения интеграции |
| `servers` | array | да | Определения DNS-серверов (каждый имеет `tag`, опциональные `address` и `detour`) |
| `rules` | array | нет | Массив правил маршрутизации; каждое правило связывает списки с DNS-сервером |
| `fallback` | array | нет | Теги серверов для использования, когда никакое правило не совпадает; если не указано, используются upstream-серверы из resolv.conf |

**DNS-правило** (элемент `rules[]`):

| Поле | Тип | По умолчанию | Описание |
|---|---|---|---|
| `enabled` | bool | true | Включено ли это правило |
| `list` | array of string | обязательно | Имена списков, домены которых должны использовать этот DNS-сервер |
| `server` | string | обязательно | Тег DNS-сервера для доменов в этих списках |
| `allow_domain_rebinding` | bool | false | Разрешить приватные IP-адреса (RFC1918) в ответах → `rebind-domain-ok` в dnsmasq |

### Установка по платформам

dnsmasq не устанавливается keen-pbr; установите его самостоятельно, если нужна эта функция.

**OpenWrt**: установите `dnsmasq`. keen-pbr добавляет UCI jail mounts (`addnmount`-записи)
в выбранную dnsmasq-секцию, чтобы бинарник мог запустить команду conf-script, и записывает каждую добавленную запись в список `keen_pbr_addnmount` этой секции. При отключении функции удаляются только записанные записи; те, что были там раньше (включая добавленные прошлыми версиями keen-pbr), keen-pbr не считает своими и не трогает.

**Keenetic (Entware)**: установите `dnsmasq-full` (заменяет встроенный ndnproxy как LAN-резолвер).
keen-pbr добавляет помеченный блок в `/opt/etc/dnsmasq.conf` с `conf-dir=/tmp/keen-pbr/dnsmasq.d,*.conf`;
скрипт инициализации `S55keen-pbr-dnsmasq` пересоздаёт tmpfs drop-in при загрузке.
Health показывает ошибку, если dnsmasq не установлен.

**Debian**: установите `dnsmasq`. keen-pbr размещает drop-in в `/etc/dnsmasq.d/keen-pbr-upstream-dns.conf`
с однострочной командой `conf-script`.

### Выбор экземпляра dnsmasq

На роутере может работать несколько экземпляров dnsmasq (на OpenWrt по одному на каждую секцию `dnsmasq` в `/etc/config/dhcp`). keen-pbr управляет ровно одним из них и опознаёт его по **селектору**:

| Платформа | Селектор | Пример |
|---|---|---|
| OpenWrt | `uci:<секция>` (имя из `uci show dhcp`) | `uci:cfg01411c`, `uci:guest` |
| Keenetic (Entware) | `entware:default` | `entware:default` |
| Debian | `systemd:<юнит>.service` | `systemd:dnsmasq.service` |

Экземпляр выбирается в таком порядке:

1. **Явно**: задан `dns.dnsmasq_instance`. Если такого экземпляра нет, keen-pbr сообщает `DNSMASQ_INSTANCE_NOT_FOUND` и ничего не меняет.
2. **По установленному drop-in**: поле пустое, а drop-in keen-pbr уже стоит ровно в одном экземпляре. Он и продолжает использоваться. Если drop-in найден в нескольких, выбор останавливается с ошибкой `DNSMASQ_INSTANCE_AMBIGUOUS`.
3. **Автоматически**: поле пустое, ничего не установлено, и ровно один включённый экземпляр обслуживает DNS (`port` не равен `0`). Если таких нет, результат `DNSMASQ_NO_DNS_INSTANCE`; если несколько, `DNSMASQ_INSTANCE_AMBIGUOUS`.

Если выбор неоднозначен, keen-pbr не гадает: ничего не меняет и не трогает уже установленные drop-in. Посмотрите кандидатов командой `keen-pbr dnsmasq detect` и задайте `dns.dnsmasq_instance` в `config.json` или в настройках DNS в веб-интерфейсе. Если drop-in keen-pbr стоит ещё и в невыбранном экземпляре, `detect` и журнал покажут предупреждение с командой для его удаления (`keen-pbr dnsmasq remove <селектор>`).

#### Определение DNS-адреса

keen-pbr больше не считает, что dnsmasq слушает `127.0.0.1:53`. Он читает конфигурацию, которую выбранный процесс действительно загрузил (файл из `-C`, его `conf-dir` вместе с фильтрами `*.conf`, аргументы командной строки), смотрит сокеты этого процесса и подтверждает адрес реальным DNS-запросом. Для проверок конфигурации и для теста маршрутизации используется тот адрес, который ответил. Если dnsmasq работает в другом сетевом пространстве имён, у него `port=0` или нет DNS-сокета, keen-pbr сообщит об ошибке, а не подставит адрес по умолчанию.

#### Проверки перед каждым перезапуском

dnsmasq перезапускается, чтобы подхватить новые настройки, поэтому keen-pbr сначала их проверяет:

- **Проверка возможностей**: один раз для каждого бинарника keen-pbr убеждается, что эта сборка dnsmasq выполняет `conf-script` в режиме `--test` (`DNSMASQ_CONF_SCRIPT_UNSUPPORTED`, `DNSMASQ_TEST_FAILED`).
- **Preflight**: keen-pbr запускает `dnsmasq --test` на реальной текущей конфигурации вместе с новым drop-in (`DNSMASQ_PREFLIGHT_FAILED`; вывод dnsmasq попадает в описание ошибки).

Если любая из проверок не прошла, новая конфигурация **отклоняется: keen-pbr не перезапускает dnsmasq и не устанавливает drop-in**, так что работающий dnsmasq продолжает жить со старыми настройками. Исправьте названную проблему и примените конфигурацию снова.

Каждый preflight лишний раз запускает генератор конфигурации, поэтому перед каждым применением в системном журнале появляется дополнительный блок `generate-resolver-config dnsmasq: pid ..., parent ... (dnsmasq)`. Так и должно быть.

#### Ошибки

Код виден в `GET /api/health/service` (`error_code`) и в веб-интерфейсе. Во всех случаях ниже keen-pbr не перезапускает dnsmasq, если в таблице не сказано иное.

| Код | Значение | Что делать |
|---|---|---|
| `DNSMASQ_NO_HOOK` | В этой сборке нет hook для dnsmasq. | Используйте пакет, собранный для вашей платформы. |
| `DNSMASQ_HOOK_FAILED` | Вызов `inspect` у hook завершился ошибкой или вывел нечитаемый ответ. | Запустите `keen-pbr dnsmasq detect`; проверьте, что hook и keen-pbr одной версии. |
| `DNSMASQ_NOT_INSTALLED` | dnsmasq не установлен. | Установите dnsmasq (см. выше). Он необязателен: без него per-list DNS-серверы недоступны. |
| `DNSMASQ_NO_DNS_INSTANCE` | Нет включённого экземпляра с DNS. | Включите экземпляр с DNS или поставьте `dns.resolver_integration` в `none`. |
| `DNSMASQ_INSTANCE_NOT_FOUND` | `dns.dnsmasq_instance` указывает на несуществующий экземпляр. | Исправьте селектор; допустимые покажет `keen-pbr dnsmasq detect`. |
| `DNSMASQ_INSTANCE_AMBIGUOUS` | Подходит несколько экземпляров. | Задайте `dns.dnsmasq_instance`. |
| `DNSMASQ_NOT_RUNNING` | Выбранный экземпляр не запущен. | Запустите dnsmasq сами. keen-pbr его не запускает и применит конфигурацию, как только dnsmasq снова заработает. |
| `DNSMASQ_DNS_DISABLED` | У выбранного экземпляра `port=0`. | Выберите другой экземпляр или включите в этом DNS. |
| `DNSMASQ_OTHER_NETNS` | dnsmasq работает в другом сетевом пространстве имён. | Не поддерживается; запустите dnsmasq в основном пространстве роутера. |
| `DNSMASQ_NO_DNS_LISTENER` | Процесс запущен, но у него нет DNS-сокета. | Проверьте `listen-address`, `interface` и `port` в конфигурации dnsmasq. |
| `DNSMASQ_ENDPOINT_UNRESPONSIVE` | Сокеты есть, но ни один не ответил на DNS-запрос. | Проверьте, что dnsmasq работает (`nslookup example.com 127.0.0.1`). Не перезапускается. |
| `DNSMASQ_DROPIN_NOT_LOADED` | Работающий dnsmasq не читает каталог, куда ставится drop-in keen-pbr (или фильтр `conf-dir` отбрасывает имя файла). | Добавьте каталог в `conf-dir` этого экземпляра или ослабьте фильтр. |
| `DNSMASQ_FOREIGN_DROPIN` | Файл drop-in существует, но его создал не keen-pbr. | Переместите или удалите файл; keen-pbr его никогда не перезаписывает. |
| `DNSMASQ_CONF_SCRIPT_UNSUPPORTED` | Эта сборка dnsmasq не выполняет `conf-script`. | Установите сборку dnsmasq с поддержкой `conf-script` (на Keenetic это `dnsmasq-full`). |
| `DNSMASQ_TEST_FAILED` | Даже минимальная конфигурация не проходит `dnsmasq --test`. | Запустите `dnsmasq --test` вручную и исправьте то, что он покажет. |
| `DNSMASQ_PREFLIGHT_FAILED` | Текущая конфигурация dnsmasq вместе с drop-in keen-pbr не проходит `--test`. | Прочитайте вывод dnsmasq в описании ошибки и исправьте конфигурацию. dnsmasq не затронут. |
| `DNSMASQ_APPLY_FAILED` | Hook не смог установить drop-in. | Смотрите вывод hook в описании ошибки и системный журнал. |
| `DNSMASQ_CONFIG_NOT_LOADED` | dnsmasq перезапущен, но новая конфигурация не появилась вовремя. | Проверьте журнал dnsmasq и примените конфигурацию ещё раз. |
| `DNSMASQ_STAMP_MISMATCH` | dnsmasq работает с другой конфигурацией, чем ожидалось (например, его перезапустил кто-то другой). | keen-pbr чинит это сам в пределах, описанных ниже. |
| `DNSMASQ_REPAIR_PAUSED` | Лимит автоматических исправлений исчерпан. | Выясните, почему dnsmasq теряет конфигурацию, затем примените конфигурацию или перезапустите keen-pbr. |
| `DNSMASQ_PROBE_FAILED` | Процесс жив, но перестал отвечать. | Проверьте dnsmasq. Он не перезапускается. |

### Как это работает

keen-pbr никогда не пишет сгенерированный конфиг dnsmasq на диск. Вместо этого:
1. keen-pbr генерирует конфиг (правила домен-список, fallback-серверы и т.д.)
2. keen-pbr размещает однострочный drop-in (`conf-script=keen-pbr generate-resolver-config dnsmasq`) в tmpfs-директорию
3. dnsmasq запускает эту команду при старте и читает конфиг из её вывода
4. dnsmasq перезапускается только когда генерируемый конфиг меняется (обновление списка, изменение настроек или один раз после старта keen-pbr) и только после проверок из раздела [Проверки перед каждым перезапуском](#проверки-перед-каждым-перезапуском)

### При остановке, отключении или удалении

- **Остановка keen-pbr**: dnsmasq НЕ перезапускается; per-list upstream-серверы остаются активными.
- **Отключение функции** (`resolver_integration: "none"`): если выключить её при работающем keen-pbr, drop-in удаляется из выбранного экземпляра, и этот экземпляр перезапускается (возврат к upstream-серверам из resolv.conf). Пока функция выключена, keen-pbr не трогает dnsmasq, в том числе при старте. Поэтому если отключить функцию и потом перезапустить keen-pbr, либо убрать интеграцию из конфига при остановленном keen-pbr, drop-in останется на месте. Он безвреден: при выключенной интеграции выводит только комментарий. Такие остатки показывает `keen-pbr dnsmasq detect`, а удаляет `keen-pbr dnsmasq remove <селектор>`. См. [команды `dnsmasq`]({{< relref "/docs/cli#dnsmasq-detect-and-remove" >}}).
- **dnsmasq не запущен**: keen-pbr никогда не запускает остановленный dnsmasq. Это изменение поведения: прежние версии его перезапускали. Если dnsmasq упал в системе без супервизора служб (Entware), запустите его вручную; keen-pbr применит свою конфигурацию заново, как только dnsmasq снова заработает.
- **dnsmasq потерял конфигурацию** (перезапущен кем-то другим, drop-in исчез): keen-pbr перезапускает его не более 3 раз (сразу, затем через 5 и 10 минут), после чего автоматическое восстановление приостанавливается; `state` в `/api/health/service` показывает `reconciling` или `error` с причиной. Применение конфигурации или перезапуск keen-pbr включают его снова. Работающий dnsmasq, который не отвечает на проверку конфигурации, не перезапускается.
- **Удаление пакета**: drop-in и любые постоянные изменения в конфиге dnsmasq удаляются; dnsmasq перезапускается.

### Решение проблем

См. [Per-list DNS servers do not work]({{< relref "/docs/troubleshooting#per-list-dns-servers-do-not-work" >}}).

## DNS Test Server (устарел)

`dns.dns_test_server` больше не запускает listener: поле принимается для
совместимости и игнорируется с предупреждением. Для проверки пути DNS
используется marker `check.keen.pbr`, который при включённом перехвате получает
ответ `127.0.0.88`. События наблюдения доступны через SSE API.

Секция `intercept` управляет перехватом DNS-ответов и L7-наблюдением:

| Поле | Тип | По умолчанию | Описание |
|---|---|---|---|
| `enabled` | boolean | `true` | Включить перехват при наличии возможностей ядра |
| `min_ttl_ms` / `max_ttl_ms` | integer | `300000` / `86400000` | Границы TTL адресов в миллисекундах, добавляемых в динамические наборы; при применении округляются вниз до целых секунд (1000–4294967295999 мс) |
| `dns.enabled` | boolean | `true` | NFQUEUE DNS hold; UDP и TCP DNS |
| `dns.queue_num` | integer | `9053` | Номер NFQUEUE |
| `dns.hold_timeout_ms` | integer | `30` | Бюджет обработки в userspace, не строгий kernel timeout |
| `dns.marker` | object | `check.keen.pbr` / `127.0.0.88` | Диагностический DNS marker |
| `l7.enabled` | boolean | `true` | NFLOG-наблюдение |
| `l7.nflog_group` | integer | `9054` | Группа NFLOG |
| `l7.tls` / `http` / `quic` | boolean | `true` | Источники SNI, HTTP Host и QUIC Initial |

Отсутствующая возможность NFQUEUE/NFLOG отключает только соответствующую
часть и отражается в health. `queue-bypass` помогает только когда listener
отсутствует; уже привязанный, но остановившийся listener может удерживать
пакеты. Полное описание failure semantics приведено на странице архитектуры.

### Требования к ядру

Релизные сборки компилируются со старыми UAPI-заголовками (Linux 3.4 для mips,
3.10 для aarch64, см. `src/netfilter/uapi_compat.hpp`), а на роутерах работают
более новые ядра (Keenetic 4.9+). В таблице указан первый mainline-релиз, в
котором появился каждый механизм. Данные взяты из истории upstream: для каждой
строки указан файл, который проверялся на последовательных тегах
[torvalds/linux](https://github.com/torvalds/linux) (`blob/<tag>/<path>`;
заголовки лежат в `include/linux/netfilter/` до v3.7 и в
`include/uapi/linux/netfilter/` после). Вендорские ядра могут бэкпортировать
или вырезать возможности, поэтому keen-pbr не полагается на эти номера во время
работы, а проверяет примитивы на деле (см. [Runtime-проверки](#runtime-проверки)).

| Возможность (для чего нужна) | Первый mainline | Где проверено (upstream) | Нужна для | Если нет |
|---|---|---|---|---|
| nfnetlink_queue: `NFQNL_CFG_CMD_BIND`, `NFQA_CFG_PARAMS`, `NFQA_PAYLOAD` в вердикте | 2.6.14 | `nfnetlink_queue.h` | DNS hold | обязательно |
| Обработчик NFQUEUE регистрируется при загрузке модуля. keen-pbr не отправляет `PF_BIND`; на более старых ядрах bind подтверждается, но пакеты никогда не ставятся в очередь | 3.8 | `net/netfilter/nfnetlink_queue_core.c`: `PF_BIND` регистрирует обработчик в v3.7 и просто `return 0` в v3.8 | DNS hold | обязательно; **не проверяется пробой** (bind проходит) |
| Цель `xt_NFQUEUE` | 2.6.16 | `net/netfilter/xt_NFQUEUE.c` (раньше `ipt_NFQUEUE`) | DNS hold, iptables | обязательно |
| `xt_NFQUEUE --queue-bypass` (`xt_NFQ_info_v2`) | 2.6.39 | `xt_NFQUEUE.h` | DNS hold, iptables | обязательно (правило не загрузится) |
| nft `queue ... bypass` (`NFT_QUEUE_FLAG_BYPASS`, `nft_queue.c`) | 3.14 | `nf_tables.h`, `nft_queue.c` | DNS hold, nft | обязательно |
| `NFQA_CFG_FLAGS`/`NFQA_CFG_MASK` + `NFQA_CFG_F_FAIL_OPEN` | 3.6 | `nfnetlink_queue.h` | DNS hold | необязательно: работа без fail-open, `fail_open=false` и предупреждение |
| `NFQA_CAP_LEN` (обнаружение усечённого захвата) | 3.7 | `nfnetlink_queue.h` | DNS hold | необязательно: усечение всё равно ловится по длине IP-заголовка |
| `NFQNL_MSG_VERDICT_BATCH` (пакетный accept при остановке) | 3.1 | `nfnetlink_queue.h` | сброс очереди при остановке | необязательно: пакеты отпускаются по одному |
| Подмена payload в вердикте в начальном user namespace | 2.6.14 (как `NFQA_PAYLOAD`) | живой тест проекта (см. [архитектуру перехвата](https://github.com/maksimkurb/keen-pbr/blob/main/docs/dns-interception.md)); upstream-коммит с ограничением по user namespace не найден | подмена marker | необязательно: marker пропускается без изменений |
| nfnetlink_log: bind/режим `NFULNL_MSG_CONFIG` | 2.6.14 | `net/netfilter/nfnetlink_log.c` | L7 | обязательно |
| Цель `xt_NFLOG` (`--nflog-group`, `--nflog-size`, `--nflog-threshold`) | 2.6.20 | `net/netfilter/xt_NFLOG.c`, `xt_NFLOG.h` | L7, iptables | обязательно |
| nft `log group ... snaplen ... queue-threshold` (`NFTA_LOG_GROUP`, `_SNAPLEN`, `_QTHRESHOLD`) | 3.13 | `nf_tables.h`, `nft_log.c` | L7, nft | обязательно |
| `NFULA_CT` (атрибуты conntrack в записях NFLOG) | 4.4 | `nfnetlink_log.h` | не используется | не требуется |
| `xt_connbytes` (`--connbytes ... packets`) | 2.6.16 | `net/netfilter/xt_connbytes.c` | L7, iptables | обязательно |
| Переключатель учёта conntrack во время работы (`nf_conntrack_acct`) | 2.6.27 | `net/netfilter/nf_conntrack_acct.c` | L7 | обязательно (keen-pbr включает его сам) |
| `xt_conntrack` `--ctdir` (`XT_CONNTRACK_DIRECTION`) | 2.6.25 | `xt_conntrack.h` | DNS hold, iptables | обязательно |
| nft `ct original packets` (`NFT_CT_PKTS`) | 4.5 | `nf_tables.h`, `nft_ct.c` | L7, nft | обязательно |
| nft `ct direction` (`NFTA_CT_DIRECTION`) | 3.13 | `nf_tables.h` | DNS hold, nft | обязательно |
| Протокол ipset netlink 6 (`IPSET_CMD_PROTOCOL`), тайм-аут элемента `IPSET_ATTR_TIMEOUT`, `hash:net` | 2.6.39 | `ipset/ip_set.h` (`IPSET_PROTOCOL 6` в v2.6.39, v3.4 и v4.9), `ipset/ip_set_hash_net.c` | запись в наборы, ipset | обязательно |
| nf_tables: batch (`NFNL_MSG_BATCH_BEGIN`), `NFT_MSG_NEWSETELEM`/`DELSETELEM` | 3.13 | `nfnetlink.h`, `nf_tables.h`, `nf_tables_api.c` | запись в наборы, nft | обязательно |
| Семейство nf_tables `inet` (`NFPROTO_INET`) | 3.14 | `include/uapi/linux/netfilter.h` | nft | обязательно |
| Тайм-аут элемента набора nf_tables (`NFT_SET_TIMEOUT`, `NFTA_SET_ELEM_TIMEOUT`) | 4.1 | `nf_tables.h` | запись в наборы, nft | обязательно |
| ctnetlink: dump и delete | 2.6.16 | `net/netfilter/nf_conntrack_netlink.c` | очистка conntrack | необязательно: очистка отключается |
| ctnetlink `CTA_ZONE` | 2.6.34 | `nfnetlink_conntrack.h` | очистка conntrack | необязательно |
| ctnetlink `CTA_TUPLE_ZONE` | 4.3 | `nfnetlink_conntrack.h` | очистка conntrack | необязательно |
| `NETLINK_NO_ENOBUFS` | 2.6.30 | `netlink.h` | настройка сокета | необязательно |
| `NETLINK_CAP_ACK` | 4.3 | `netlink.h` | настройка сокета | необязательно (ошибка `setsockopt` игнорируется) |
| `NETLINK_EXT_ACK` | 4.12 | `netlink.h` | настройка сокета | необязательно (ошибка `setsockopt` игнорируется) |
| ioctl `NS_GET_USERNS` (возможность подмены) | 4.9 | `include/uapi/linux/nsfs.h` | подмена marker | необязательно: вне начального namespace `payload_replacement` равен `unknown` |
| nft `numgen` (только балансировка, не перехват) | 4.9 | `net/netfilter/nft_numgen.c` | outbound `balance`, nft | вне этой страницы |

#### Минимум для каждого бэкенда

| Бэкенд | DNS hold | L7-наблюдение | Полный набор |
|---|---|---|---|
| iptables | 3.8 (регистрация обработчика очереди; строки `--queue-bypass`, ipset и `--ctdir` старше) | 2.6.39 (ipset); модули L7 старше | 3.8 |
| nftables | 4.1 (тайм-аут элемента набора) | 4.5 (`ct original packets`) | 4.5 (4.9 для балансировки outbound) |

Fail-open требует 3.6; любое ядро, подходящее под минимум DNS hold для iptables
(3.8), его уже имеет, поэтому проба `fail_open` может не пройти там только на
вендорских ядрах, где возможность вырезана.

«Обязательно» означает, что при отсутствии возможности соответствующая часть
отключается и это отражается в health; сам keen-pbr продолжает работать, а
другая часть остаётся активной. «Необязательно» означает, что часть работает с
урезанным поведением, предупреждением в `/api/health/service` и строкой в логе.

#### Runtime-проверки

Номера версий говорят о том, что выпустил upstream, но не о том, что делает
конкретное ядро роутера. Поэтому после проверки `/proc/net/ip*_tables_*`
(только iptables) keen-pbr выполняет каждый примитив на деле и выводит результат
в `intercept.probes` ответа `GET /api/health/service`. Проверки не затрагивают
пользовательский трафик и никогда не приводят к ошибке применения конфигурации.

| Проба | Запрос | Результат |
|---|---|---|
| `set_backend` | ipset: `IPSET_CMD_PROTOCOL` (нужен ответ не ниже протокола 6). nftables: `NEWSETELEM` в batch в несуществующий набор (`ENOENT` доказывает, что nf_tables отвечает; `EOPNOTSUPP`/`EINVAL` — что нет) | `unsupported`/`error` отключает DNS hold и L7 |
| `nfqueue` | bind настроенной очереди и `NFQA_CFG_PARAMS`, выполняются самим сервисом | сбой отключает только DNS hold |
| `fail_open` | `NFQA_CFG_FLAGS(FAIL_OPEN)` на привязанной очереди и контрольный запрос с неизвестным битом флага. Ядра до 3.6 игнорируют атрибут и подтверждают оба запроса, поэтому принятый контрольный запрос означает, что флаг не разбирался | `unsupported`: очередь работает без fail-open, `capabilities.fail_open=false`, предупреждение |
| `payload_replacement` | владелец network namespace через `NS_GET_USERNS` | `supported`, `unsupported` или `unknown`; информационно |
| `nflog` | bind настроенной группы, выполняется самим сервисом | сбой отключает только L7 |
| `set_write` | однократно при запуске сервиса: добавление и удаление во временном проверочном наборе (`kpbr4d_keenpbrprobe` для ipset или таблица nft `KeenPbrProbe`); после этого проверочный объект удаляется | при сбое DNS hold и L7 отключаются до установки правил. Перезапустите сервис, чтобы повторить проверку возможностей ядра. Ошибки записи в активные наборы во время работы сообщаются отдельно и не перепроверяют возможность |
| `conntrack` | запрос дампа ctnetlink, прерываемый после первого ответа | сбой отключает только очистку conntrack (предупреждение) |

`kernel_release` (`uname -r`) выводится для информации и не влияет на решения.
Проверки возможностей ядра выполняются один раз до установки firewall и
кэшируются до перезапуска сервиса. Результаты bind listener — отдельный
механизм: неудачный bind NFQUEUE/NFLOG может повториться при runtime refresh
или применении конфигурации. Запись в активные наборы во время работы
отслеживается отдельно.

## DNS-серверы

Каждый сервер имеет тег, опциональный `type`, опциональный `address` и опциональный `detour`.
Значения тега DNS-сервера должны соответствовать `^[a-z][a-z0-9_]*$`, быть не более 24 символов и должны быть уникальными.

| Поле | Тип | Обязательно | Описание |
|---|---|---|---|
| `tag` | string | да | Уникальный идентификатор для этого DNS-сервера |
| `type` | string | нет | Тип источника DNS: `static` (по умолчанию) или `keenetic` |
| `address` | string | для `static` | IP-адрес DNS-сервера с опциональным портом, например `"10.8.0.1"`, `"10.8.0.1:5353"`, `"2001:4860:4860::8888"` или `"[2001:4860:4860::8888]:5353"` |
| `detour` | string | нет | Outbound для использования при обращении к этому DNS-серверу |

Поле `detour` полезно, когда DNS-сервер должен быть доступен через конкретное соединение, обычно тот же VPN, который будет переносить соответствующий трафик.

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "vpn_dns",
        "type": "static",
        "address": "10.8.0.1:5353",
        "detour": "vpn"
      },
      {
        "tag": "google_dns",
        "address": "8.8.8.8"
      },
      {
        "tag": "google_dns_v6",
        "address": "[2001:4860:4860::8888]:53"
      },
      {
        "tag": "keenetic_dns",
        "type": "keenetic"
      }
    ]
  }
}
```

### `type: keenetic` (встроенный DNS роутера через RCI)

На роутерах Keenetic, `type: "keenetic"` сообщает keen-pbr повторно использовать текущие встроенные настройки DNS роутера автоматически.

Правила и поведение:

- Разрешён максимум один элемент в `dns.servers` с `type: "keenetic"`.
- Для `type: "keenetic"` поле `address` не должно быть задано (адреса берутся из RCI).
- keen-pbr читает неспециализированные (unscoped) строки `dns_server = ...` из политики прокси **System**.
- Если есть неспециализированные зашифрованные апстримы (DoH/DoT), используются все такие серверы по порядку.
- Иначе используются все неспециализированные plaintext-апстримы по порядку.

### Как работает `detour`

Когда задан `detour`, DNS-запросы самого keen-pbr к upstream-серверу направляются через выбранный outbound. Правило firewall сопоставляет IP-адрес и порт DNS-сервера; оно не применяет detour к транзитным запросам клиентов на тот же адрес.

Например, если `vpn_dns` имеет `detour: "vpn"`, тогда DNS-запросы к `vpn_dns` также пойдут через `vpn`.

## Полный пример

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "vpn_dns",
        "address": "10.8.0.1:5353",
        "detour": "vpn"
      },
      {
        "tag": "google_dns",
        "address": "8.8.8.8"
      },
      {
        "tag": "google_dns_v6",
        "address": "[2001:4860:4860::8888]:53"
      }
    ]
  }
}
```
