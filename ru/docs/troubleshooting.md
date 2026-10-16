# Устранение неполадок

Начните с логов и статуса сервисов, затем переходите к DNS, firewall, таблицам маршрутизации и интерфейсам. В `keen-pbr` 3.x демон пишет причины ошибок в системный журнал, поэтому логи почти всегда быстрее угадывания по симптомам.

## Быстрый порядок диагностики

1. Проверьте системный журнал на ошибки `keen-pbr`.
2. Проверьте, что сервисы `keen-pbr` запущены.
3. Если `keen-pbr` падает при запуске, запустите его вручную в терминале, чтобы увидеть больше логов: `keen-pbr --log-level verbose service`.
4. Проверьте DNS: устройство должно использовать DNS роутера, а настроенный резолвер должен отвечать локально.
5. Проверьте firewall: правила `keen-pbr` должны быть в `KeenPbrTable`.
6. Проверьте маршрутизацию по {{< term "fwmark" >}}: `fwmark` должен вести в нужную таблицу маршрутизации.
7. Проверьте интерфейсы и VPN-туннели.
8. Если проблема остаётся, проверьте удалённые списки, `urltest`, фильтры правил и конфликты низкоуровневой маршрутизации.

## Системный журнал

Это первое место для диагностики. Ищите сообщения `keen-pbr`, а также уровни `[E]` и `[W]`: так `keen-pbr` помечает ошибки и предупреждения. Если не хватает библиотек `iptables` / `nftables`, не найден интерфейс, занят порт API или сломан JSON, причина обычно будет написана именно здесь.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
В веб-интерфейсе Keenetic откройте **Диагностика** → **Системный журнал**.

Из консоли можно прочитать журнал так:

```bash {filename="bash"}
ndmc -c "show log once" | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
logread | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
journalctl -u keen-pbr
```

Если нужен только текущий запуск:

```bash {filename="bash"}
journalctl -u keen-pbr -b
```
{{< /tab >}}
{{< /tabs >}}

## Сервис не запускается {#service-does-not-start}

В этом сценарии не начинайте с редактирования конфигурации вслепую. Сначала проверьте статус и сразу откройте логи.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
1. Убедитесь, что конфигурация существует:
   ```bash {filename="bash"}
   ls -l /opt/etc/keen-pbr/config.json
   ```
2. Перезапустите `keen-pbr`:
   ```bash {filename="bash"}
   /opt/etc/init.d/S80keen-pbr restart
   ```
3. Проверьте статус `keen-pbr`:
   ```bash {filename="bash"}
   /opt/etc/init.d/S80keen-pbr status
   ```
4. Прочитайте системный журнал:
   ```bash {filename="bash"}
   ndmc -c "show log once" | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
   ```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
1. Убедитесь, что конфигурация существует:
   ```bash {filename="bash"}
   ls -l /etc/keen-pbr/config.json
   ```
2. Перезапустите `keen-pbr`:
   ```bash {filename="bash"}
   service keen-pbr restart
   ```
3. Проверьте статус `keen-pbr`:
   ```bash {filename="bash"}
   service keen-pbr status
   ```
4. Прочитайте системный журнал:
   ```bash {filename="bash"}
   logread | grep -E 'keen-pbr|\[E\]|\[W\]|error|warn|warning'
   ```
{{< /tab >}}
{{< tab name="Debian" >}}
1. Убедитесь, что конфигурация существует:
   ```bash {filename="bash"}
   ls -l /etc/keen-pbr/config.json
   ```
2. Перезапустите `keen-pbr`:
   ```bash {filename="bash"}
   systemctl restart keen-pbr
   ```
3. Проверьте статус `keen-pbr`:
   ```bash {filename="bash"}
   systemctl status keen-pbr
   ```
4. Прочитайте системный журнал:
   ```bash {filename="bash"}
   journalctl -u keen-pbr -b
   ```
{{< /tab >}}
{{< /tabs >}}

Если `keen-pbr` не остаётся запущенным, остановите сервис и запустите keen-pbr вручную в терминале с подробными логами. Так ошибка появится прямо в консоли.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr stop
keen-pbr --log-level verbose service
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
service keen-pbr stop
keen-pbr --log-level verbose service
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
systemctl stop keen-pbr
keen-pbr --log-level verbose service
```
{{< /tab >}}
{{< /tabs >}}

Пока keen-pbr работает, команда не вернёт вас в командную строку, так и должно быть. Если она сразу завершилась, последняя ошибка в выводе обычно и есть причина.

{{% details title="Расширенные проверки" closed="true" %}}
1. Проверьте JSON:

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
jq . /opt/etc/keen-pbr/config.json
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
jq . /etc/keen-pbr/config.json
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
jq . /etc/keen-pbr/config.json
```
{{< /tab >}}
{{< /tabs >}}

2. Убедитесь, что каталог для `daemon.pid_file` существует и доступен для записи.
3. Убедитесь, что `daemon.cache_dir` существует и доступен для записи.
4. Если API включён, проверьте, что адрес и порт из `api.listen` не заняты другим процессом.
5. Если в логах есть ошибка бэкенда firewall, проверьте установку `iptables` / `ipset` или `nftables` для вашей платформы.
6. Если `keen-pbr` жив, но Web UI не открывается, убедитесь, что конфигурация не заменена headless-примером и в `config.json` есть секция `api`.
{{% /details %}}

## Сайты не идут через VPN

1. Убедитесь, что устройство пользователя использует DNS роутера.
   - Откройте `http://<ip-роутера>:12121/` и посмотрите на виджет проверки DNS. Проверочный запрос должен дойти до резолвера роутера и вернуть `127.0.0.88`.
   - Альтернативно, выполните с вашего ПК: `nslookup check.keen-pbr.fyi`. Должен вернуться `127.0.0.88`.
2. Запустите тест маршрутизации:
   - Откройте `http://<ip-роутера>:12121/` и введите домен или IP в виджет "Куда пойдёт этот трафик?".
   - Альтернативно, выполните с роутера или сервера:
     ```bash {filename="bash"}
     keen-pbr test-routing google.com
     ```
3. Убедитесь, что домен или IP находится в правильном списке.
4. Убедитесь, что правило маршрутизации для этого списка указывает на нужный outbound.
5. Убедитесь, что VPN-интерфейс действительно поднят и пропускает трафик.

Если ожидаемый и фактический outbound различаются, переходите последовательно к разделам DNS, firewall и маршрутизации ниже.

## DNS-перехват

Для маршрутизации по доменам keen-pbr не нужен особый резолвер: он сам читает DNS-ответы, проходящие через роутер, и записывает адреса в наборы списков. Состояние перехвата и причины, по которым он может быть отключён, смотрите в `/api/health/service` (`intercept`).

### Проверка DNS с устройства пользователя

Откройте `http://<ip-роутера>:12121/` и посмотрите на виджет проверки DNS. Если Web UI недоступен, выполните с клиентского устройства:

```bash {filename="bash"}
nslookup check.keen-pbr.fyi
```

Ожидаемый ответ: `127.0.0.88`. У домена есть и настоящий публичный адрес, поэтому по ответу видно, что произошло:

- `127.0.0.88`: запрос прошёл через роутер, и keen-pbr его увидел.
- Любой другой адрес: DNS работает, но идёт мимо роутера (например, браузер или ОС используют свой защищённый DNS), либо DNS-перехват недоступен.
- Нет ответа или ошибка: на устройстве не работает DNS или резолвер роутера не отвечает.

### Проверка резолвера на роутере

keen-pbr работает с любым резолвером, который использует роутер. Проверьте, что он отвечает локально:

```bash {filename="bash"}
nslookup google.com 127.0.0.1
```

Если вы видите `Connection refused`, ваш резолвер не запущен или не слушает `127.0.0.1:53`. Проверьте его собственный сервис и журнал (например {{< term "Entware" >}} `dnsmasq` на Keenetic, `dnsmasq` на OpenWrt и Debian или встроенный DNS-прокси Keenetic).

На Keenetic, если DNS-запросы клиентов не доходят до Entware `dnsmasq`, проверьте настройку, специфичную для Keenetic:

```bash {filename="bash"}
opkg dns-override
```

После изменения сохраните конфигурацию Keenetic:

```bash {filename="bash"}
system configuration save
```

{{% details title="После обновления со старой версии keen-pbr" closed="true" %}}
Старые версии keen-pbr всегда настраивали dnsmasq, и после обновления управление остаётся включённым, подробности в разделе [Обновление с 3.3.x]({{< relref "/docs/changelog#upgrading-from-33x" >}}). Если dnsmasq не запускается после того, как вы удалили keen-pbr вручную, убедитесь, что в его конфигурации не осталось строки `conf-script=...keen-pbr...`.
{{% /details %}}

### Измерение потерь при DNS-перехвате

Если первое соединение с только что открытым сайтом иногда уходит мимо VPN, проверьте, как часто keen-pbr не успевает записать адрес до того, как DNS-ответ дойдёт до устройства. Для этого есть скрипт `scripts/dns-bench.py` (нужен только Python 3). Запустите его с компьютера в локальной сети или с самого роутера:

```bash {filename="bash"}
python3 scripts/dns-bench.py --api http://192.168.1.1:12121 --password 'пароль-администратора' \
  --rates 50,200,1000 --count 1000
```

Скрипт запрашивает случайные имена вида `a1b2c3d4-198-18-7-9.nip.io`. Они указывают на тестовые адреса `198.18.0.0/15`, поэтому реальный трафик не затрагивается. Чтобы keen-pbr их обрабатывал, добавьте в конфигурацию список и правило (если их нет, скрипт подскажет, что добавить):

```json
"lists": { "dns_bench": { "domains": ["nip.io"] } },
"route": { "rules": [ { "list": ["dns_bench"], "outbound": "<любой тег outbound>" } ] }
```

Публичный сервис `nip.io` может ограничивать число запросов, тогда в отчёте появятся `client-timeout`.

Основные результаты в отчёте:

| Результат | Что значит |
|---|---|
| `held-ok` | Всё в порядке: адрес записан до того, как ответ ушёл устройству |
| `hold-timeout` | keen-pbr не успел за `intercept.dns.hold_timeout_ms`: ответ ушёл раньше, адрес дописан чуть позже. Первые пакеты могли пройти мимо VPN |
| `set-error` | Адрес не удалось записать в набор, смотрите логи keen-pbr |
| `bypass` | Ответ прошёл мимо keen-pbr: имя не входит в списки или очередь переполнена |
| `not-in-set`, `wrong-set` | После прогона адреса нет в наборе или он в наборе другого outbound |
| `client-timeout` | Устройство не получило ответа |

Если `hold-timeout` много, роутеру не хватает процессора на запись адресов: попробуйте увеличить `intercept.dns.hold_timeout_ms`. Описание всех опций выводит `python3 scripts/dns-bench.py --help`.

## Веб-сайты не открываются: `DNS_PROBE_FINISHED_NXDOMAIN` / `ERR_NAME_NOT_RESOLVED`

keen-pbr не разрешает имена для клиентов, поэтому проверьте резолвер, которым пользуются клиенты.

1. Убедитесь, что устройство пользователя использует DNS роутера.
2. Убедитесь, что у резолвера роутера есть рабочие upstream-серверы и он может до них достучаться.
3. Проверьте с роутера: `nslookup google.com 127.0.0.1`.
4. Если запись в `dns.servers` использует `detour`, проверьте выбранный outbound.

## Веб-сайты не открываются: `DNS_PROBE_FINISHED_BAD_CONFIG`

Обычно это означает, что резолвер не запущен или не смог применить конфигурацию. Проверьте сервис и журнал резолвера на вашей платформе (например {{< term "Entware" >}} `dnsmasq` на Keenetic, `dnsmasq` на OpenWrt и Debian) и состояние перехвата (`intercept`) в `/api/health/service`.

## Не работают отдельные DNS-серверы для списков {#per-list-dns-servers-do-not-work}

Когда включена `dns.resolver_integration: "dnsmasq"`, домены из определённых списков должны разрешаться через выбранный DNS-сервер. Если это не происходит:

1. **Проверьте состояние dnsmasq в `/api/health/service`:**
   ```bash {filename="bash"}
   curl http://127.0.0.1:12121/api/health/service | jq .dnsmasq
   ```
   Ожидается: `"state": "ok"`, `"rules"` показывает количество, `"domains"` показывает записи доменов. Если `state` равен `error`, смотрите `last_error`.

2. **Проверьте сгенерированный конфиг dnsmasq:**
   ```bash {filename="bash"}
   keen-pbr generate-resolver-config dnsmasq
   ```
   Ожидается: соответствия «домен → сервер», резервные (`fallback`) серверы и исключения защиты от {{< term "DNS rebinding" >}}. Если в выводе только комментарий о выключенном управлении и строки `server=`, проверьте, что установлен `resolver_integration: "dnsmasq"` и настроено хотя бы одно правило.

3. **Проверьте файл drop-in dnsmasq:**

   {{< tabs >}}
   {{< tab name="OpenWrt" selected=true >}}
   ```bash {filename="bash"}
   cat /tmp/dnsmasq.d/keen-pbr.conf
   # или в dnsmasq-confdir по платформе:
   cat /tmp/dnsmasq.*/keen-pbr.conf
   ```
   {{< /tab >}}
   {{< tab name="Keenetic / Entware" >}}
   ```bash {filename="bash"}
   grep -A 5 "BEGIN keen-pbr" /opt/etc/dnsmasq.conf
   ```
   {{< /tab >}}
   {{< tab name="Debian" >}}
   ```bash {filename="bash"}
   grep -A 3 "BEGIN keen-pbr" /etc/dnsmasq.conf
   cat /etc/keen-pbr/dnsmasq.d/keen-pbr.conf
   ```
   {{< /tab >}}
   {{< /tabs >}}

   Ожидается: строка `conf-script=keen-pbr generate-resolver-config dnsmasq`.

4. **Проверьте, что dnsmasq действительно загрузил конфигурацию keen-pbr:**
   ```bash {filename="bash"}
   nslookup -type=txt config-hash.keen.pbr 127.0.0.1
   ```
   Ожидается: TXT-ответ вида `<hash>|<boottime_ms>|<unix_ts>`; хеш должен совпадать с `config_hash` из шага 1. Нет ответа - значит, dnsmasq не загрузил конфигурацию keen-pbr (например, {{< term "drop-in" >}} лежит в каталоге, который dnsmasq не читает, или dnsmasq не был перезапущен).

5. **Убедитесь, что dnsmasq действительно используется как резолвер для клиентов:**
   - На Keenetic проверьте, является ли ndnproxy или dnsmasq LAN-резолвером:
     ```bash {filename="bash"}
     opkg dns-override
     ```
   - Тест с клиента:
     ```bash {filename="bash"}
     nslookup example.com
     ```

6. **Проверьте `detour` DNS-сервера:**
   Если сервер имеет `"detour": "vpn"`, убедитесь, что VPN-интерфейс работает и доступен:
   ```bash {filename="bash"}
   curl http://127.0.0.1:12121/api/runtime/outbounds | jq '.[] | select(.tag == "vpn")'
   ```
   Ожидается: `"state": "alive"`.

7. **Проверьте, что домены в правильном списке:**
   - Откройте Web UI на `http://<router-ip>:12121/` и проверьте DNS-правила.
   - Убедитесь, что нужные домены действительно в списке.

## Firewall и `KeenPbrTable`

Если DNS работает и домен резолвится, но трафик всё равно идёт мимо VPN, проверьте firewall. `keen-pbr` создаёт изолированную цепочку или таблицу `KeenPbrTable`; трафик должен попадать туда, сопоставляться со списками и получать нужный `fwmark`.

Сначала запустите встроенную проверку:

```bash {filename="bash"}
keen-pbr status
```

Ищите проверки firewall со статусом `MISSING`, `MISMATCH` или `ERROR`.

### Проверка правил firewall

{{< tabs >}}
{{< tab name="iptables / ipset" selected=true >}}
```bash {filename="bash"}
iptables-save | grep KeenPbrTable
ip6tables-save | grep KeenPbrTable
```

Ожидаемо, есть переход из `PREROUTING` в `KeenPbrTable` и правила, которые ставят {{< term "fwmark" >}} (`MARK`). Пример признака корректного правила:

```text
-A KeenPbrTable -m set --match-set <set> dst -j MARK --set-xmark <mark>/<mask>
```

Чтобы проверить содержимое набора:

```bash {filename="bash"}
ipset list
ipset test <название_сета> <IP-адрес>
```

Ожидаемый ответ: IP есть в указанном наборе.
{{< /tab >}}
{{< tab name="nftables / nftset" >}}
```bash {filename="bash"}
nft -t list ruleset
```

Если нужен полный вывод вместе с содержимым наборов:

```bash {filename="bash"}
nft list ruleset
```

Ожидаемо, есть таблица `inet KeenPbrTable`, hook `prerouting`, правила, проверяющие наборы, и действие `meta mark set ...`.
{{< /tab >}}
{{< tab name="Debian" >}}
Бэкенд зависит от того, как установлен firewall на вашей системе. Проверьте оба варианта или тот, который указан в логах `keen-pbr`.

```bash {filename="bash"}
iptables-save | grep KeenPbrTable
ip6tables-save | grep KeenPbrTable
nft -t list ruleset
```
{{< /tab >}}
{{< /tabs >}}

Если `KeenPbrTable` отсутствует, вернитесь к логам `keen-pbr`: обычно причина в недоступном бэкенде, правах, отсутствующих пакетах или ошибке конфигурации.

## Таблицы маршрутизации и `fwmark`

Если firewall ставит пакетам {{< term "fwmark" >}}, но сайт бесконечно грузится или открывается через провайдера, проверьте маршрутизацию по fwmark. ОС должна увидеть `fwmark`, применить `ip rule` и отправить пакет в таблицу маршрутизации нужного outbound.

1. Проверьте состояние, которое ожидает `keen-pbr`:
   ```bash {filename="bash"}
   keen-pbr status
   ```
   Ищите строки со статусами `MISSING`, `MISMATCH` или `ERROR`.

2. Проверьте конкретный домен или IP:
   ```bash {filename="bash"}
   keen-pbr test-routing google.com
   ```
   Ожидаемо, ожидаемый и фактический outbound совпадают.

3. Проверьте записи `ip rule`:
   ```bash {filename="bash"}
   ip rule show
   ```
   Ожидаемо, есть правило вида `fwmark <mark> lookup <table>`.

4. Проверьте таблицу маршрутизации:
   ```bash {filename="bash"}
   ip route show table <номер_таблицы>
   ```
   Ожидаемо, в таблице есть маршрут через нужный VPN-интерфейс или шлюз. Для outbound типа `blackhole` ожидается маршрут `blackhole`.

Если таблица пустая, интерфейс не найден или правило ведёт не туда, проверьте имя outbound, имя интерфейса и конфликты `fwmark` / `iproute.table_start`.

## Маршрутизация ломается при включённом Tailscale {#tailscale}

Tailscale резервирует `0x00FF0000`, что пересекается с маской keen-pbr по
умолчанию, и восстанавливает {{< term "connmark" >}} из {{< term "conntrack" >}}, полностью перезаписывая {{< term "fwmark" >}}
пакета. Перенос keen-pbr в другой диапазон fwmark не мешает Tailscale стереть его.

Отключите netfilter-интеграцию Tailscale:

```bash {filename="bash"}
tailscale set --netfilter-mode=off
```

После этого правила firewall и {{< term "NAT" >}} нужно настраивать отдельно, особенно если
Tailscale работает как subnet router или exit node.

## Интерфейсы и VPN-туннели

Если DNS, firewall и маршрутизация по {{< term "fwmark" >}} выглядят правильно, проверьте, что сам интерфейс существует, поднят и может отправлять трафик.

1. Откройте `http://<ip-роутера>:12121/` и проверьте состояние outbounds и интерфейсов.
2. Получите список интерфейсов через REST API:
   ```bash {filename="bash"}
   curl http://127.0.0.1:12121/api/runtime/interfaces
   ```
3. Сверьте системное состояние:
   ```bash {filename="bash"}
   ip link show
   ip addr show
   ip route
   ```
4. Проверьте выход через конкретный VPN-интерфейс:
   ```bash {filename="bash"}
   curl -v --interface <имя_интерфейса> https://ifconfig.co/json
   ```

Ожидаемо, `curl` возвращает внешний IP VPN. Если команда зависает или завершается ошибкой, проблема ниже `keen-pbr`: туннель не поднят, нет маршрута, недоступен шлюз или блокируется исходящий трафик.

{{% details title="Если используется urltest или icmptest" closed="true" %}}
1. Проверьте, что у каналов (outbounds из `outbound_groups`) правильные интерфейсы или таблицы.
2. Посмотрите состояние каналов на странице **Outbounds** веб-интерфейса или в `GET /api/runtime/outbounds`.
3. Если канал показан как `unavailable`, keen-pbr исключил его после неудачных проверок. Дождитесь `circuit_breaker.timeout_ms` или почините канал.
4. Если резервный канал не включается, сначала проверьте каждый канал отдельно через `curl --interface`.
{{% /details %}}

## Удалённые списки не обновляются

1. Выполните на роутере или сервере:
   ```bash {filename="bash"}
   keen-pbr download
   ```
2. Если список всё ещё не обновляется, проверьте, достижим ли URL с этой же системы.
3. Если список должен скачиваться через VPN, проверьте `lists[].detour` и соответствующий outbound.
4. Если используется автоматическое обновление, проверьте `lists_autoupdate.cron`.
5. Если список не скачался при первом запуске, демон повторяет попытки в фоне (через 10 секунд, 30 секунд, 2 минуты, затем каждые 5 минут), пока загрузка не удастся. Если включены отдельные DNS-серверы для списков, dnsmasq настраивается при старте до первой загрузки. Если домен сервера со списком должен разрешаться через отдельный DNS-сервер, добавьте этот домен во встроенный список, файловый список или уже закэшированный список, иначе первая попытка пойдёт через обычный DNS.
6. После ошибки снова прочитайте логи `keen-pbr`.

{{% details title="Расширенные проверки" closed="true" %}}
Если нужно принудительно выполнить полную перезагрузку:

```bash {filename="bash"}
kill -HUP $(cat /var/run/keen-pbr.pid)
```

Если в конфигурации задан другой `daemon.pid_file`, используйте его путь. Также подтвердите, что `daemon.cache_dir` доступен для записи.
{{% /details %}}

## `urltest` всегда показывает «Деградирован»

1. Убедитесь, что тестовый `url` доступен и возвращает код `2xx`, например `200 OK` или `204 No Content`. Адрес по умолчанию: `https://www.gstatic.com/generate_204`.
2. Если HTTPS-проверки на роутере не проходят (неверное системное время, устаревшие корневые сертификаты, ограничения TLS), попробуйте HTTP-адрес, например `http://www.gstatic.com/generate_204`.
3. Убедитесь, что каждый канал работает сам по себе.
4. Проверьте интерфейсы через `curl --interface <имя_интерфейса> https://ifconfig.co/json`.
5. Дождитесь следующей проверки или временно уменьшите `interval_ms` на время диагностики.
6. Проверьте `GET /api/runtime/outbounds`. Если канал в состоянии `unavailable`, keen-pbr его исключил; дождитесь `circuit_breaker.timeout_ms`.

## Не срабатывают правила с фильтром портов или адресов

{{< callout type="warning" >}}
Если вы используете отрицание в полях `src_addr` / `dest_addr`, отрицание применяется сразу ко всем указанным в этом поле IP/подсетям. Смешивание записей с отрицанием и без отрицания в одном списке невозможно. Как альтернатива, вы можете создать два отдельных правила.

Это же применимо к `src_port` / `dest_port`.
{{< /callout >}}

Если правила срабатывают не так, как ожидалось:

- Убедитесь, что `proto` установлен корректно: `null` для любых протоколов, `"tcp"`, `"udp"` или `"tcp/udp"`.
- Проверьте, что имя списка в правиле точно совпадает с ключом в `lists`, включая регистр.
- Запустите `keen-pbr test-routing <domain-or-ip>` и сравните ожидаемый и фактический outbound.
- Проверьте `keen-pbr status`, чтобы увидеть, созданы ли правила firewall для этого правила маршрутизации.

## Конфликты низкоуровневой маршрутизации

Если вы изменили настройки `fwmark` или `iproute`, или другой инструмент управляет маршрутизацией по {{< term "fwmark" >}} (`ip rule`) на той же системе, пакеты могут быть неправильно направлены или отброшены.

Проверьте на конфликты:

```bash {filename="bash"}
keen-pbr status
ip rule show
ip route show table all
```

Для правил firewall с fwmark:

{{< tabs >}}
{{< tab name="iptables / ipset" selected=true >}}
```bash {filename="bash"}
iptables-save | grep -E 'MARK|CONNMARK|KeenPbrTable'
ip6tables-save | grep -E 'MARK|CONNMARK|KeenPbrTable'
```
{{< /tab >}}
{{< tab name="nftables / nftset" >}}
```bash {filename="bash"}
nft -t list ruleset | grep -E 'mark|KeenPbrTable'
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
iptables-save | grep -E 'MARK|CONNMARK|KeenPbrTable'
ip6tables-save | grep -E 'MARK|CONNMARK|KeenPbrTable'
nft -t list ruleset | grep -E 'mark|KeenPbrTable'
```
{{< /tab >}}
{{< /tabs >}}

Настройте `fwmark` на неконфликтующий диапазон:

```json {filename="config.json"}
{
  "fwmark": {
    "start": "0x00020000",
    "mask": "0x00FF0000"
  }
}
```

`mask` должна состоять из идущих подряд шестнадцатеричных цифр `F`. Используйте hex-строки, например `"0x00FF0000"` и `"0x00020000"`.

