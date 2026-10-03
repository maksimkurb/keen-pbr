---
title: Устранение неполадок
weight: 7
aliases:
  - /docs/troubleshooting/troubleshooting/
---

Начните с логов и статуса сервисов, затем переходите к DNS, firewall, таблицам маршрутизации и интерфейсам. В `keen-pbr` 3.x демон активно пишет причины ошибок в системный журнал, поэтому логи почти всегда быстрее угадывания по симптомам.

## Быстрый порядок диагностики

1. Проверьте системный журнал на ошибки `keen-pbr`.
2. Проверьте, что сервисы `keen-pbr` запущены.
3. Если `keen-pbr` падает при запуске, запустите его вручную в foreground-режиме, чтобы увидеть больше логов: `keen-pbr --log-level verbose service`.
4. Проверьте DNS: устройство должно использовать DNS роутера, а настроенный резолвер должен отвечать локально.
5. Проверьте firewall: правила `keen-pbr` должны быть в `KeenPbrTable`.
6. Проверьте policy routing: `fwmark` должен вести в нужную таблицу маршрутизации.
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

## Сервис не запускается

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

Если `keen-pbr` не остаётся запущенным, остановите managed service и запустите демон вручную в foreground-режиме с подробными логами. Так ошибка появится прямо в консоли.

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

Ожидаемо, foreground-команда не вернёт prompt, пока демон работает. Если она сразу завершилась, последняя ошибка в выводе обычно и есть причина.

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
5. Если в логах есть ошибка про firewall backend, проверьте установку `iptables` / `ipset` или `nftables` для вашей платформы.
6. Если `keen-pbr` жив, но Web UI не открывается, убедитесь, что конфигурация не заменена headless-примером и в `config.json` есть секция `api`.
{{% /details %}}

## Сайты не идут через VPN

1. Убедитесь, что устройство пользователя использует DNS роутера.
   - Откройте `http://<ip-роутера>:12121/` и посмотрите на виджет проверки DNS. Marker-запрос должен достичь резолвера роутера и вернуть `127.0.0.88`.
   - Альтернативно, выполните с вашего ПК: `nslookup check.keen.pbr`. Должен вернуться `127.0.0.88`.
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

keen-pbr не настраивает и не требует никакого резолвера: DNS-ответы
удерживаются в NFQUEUE, а адреса из совпавших списков напрямую записываются в
динамические наборы. См. [архитектуру DNS-перехвата](https://github.com/maksimkurb/keen-pbr/blob/main/docs/dns-interception.md)
для queue, marker, timeout и capability semantics; счётчики перехвата и причины
capability смотрите в `/api/health/service` (`intercept`).

### Проверка DNS с устройства пользователя

Откройте `http://<ip-роутера>:12121/` и проверьте DNS Check. Если Web UI недоступен, выполните с клиентского устройства:

```bash {filename="bash"}
nslookup check.keen.pbr
```

Ожидаемый ответ: `127.0.0.88`. Если ответа нет, устройство не использует DNS роутера, резолвер не отвечает или DNS-перехват недоступен.

### Проверка резолвера на роутере

keen-pbr работает с любым резолвером, который использует роутер. Проверьте, что он отвечает локально:

```bash {filename="bash"}
nslookup google.com 127.0.0.1
```

Если вы видите `Connection refused`, ваш резолвер не запущен или не слушает `127.0.0.1:53`. Проверьте его собственный сервис и журнал (например Entware `dnsmasq` на Keenetic, `dnsmasq` на OpenWrt и Debian или встроенный DNS-прокси Keenetic).

На Keenetic, если DNS-запросы клиентов не доходят до Entware `dnsmasq`, проверьте настройку, специфичную для Keenetic:

```bash {filename="bash"}
opkg dns-override
```

После изменения сохраните конфигурацию Keenetic:

```bash {filename="bash"}
system configuration save
```

{{% details title="После обновления с интеграции dnsmasq" closed="true" %}}
Старые версии управляли dnsmasq. При обновлении пакет удаляет hook `conf-script` keen-pbr и возвращает ваши upstream-серверы; см. заметки об обновлении в разделе [DNS]({{< relref "/docs/configuration/dns" >}}). На Keenetic в `/opt/etc/dnsmasq.conf` мог быть добавлен блок `# BEGIN keen-pbr fallback upstream`: проверьте его. Если dnsmasq не запускается, убедитесь, что в его конфигурации не осталось строки `conf-script=...keen-pbr...`.
{{% /details %}}

## Веб-сайты не открываются: `DNS_PROBE_FINISHED_NXDOMAIN` / `ERR_NAME_NOT_RESOLVED`

keen-pbr не разрешает имена для клиентов, поэтому проверьте резолвер, которым пользуются клиенты.

1. Убедитесь, что устройство пользователя использует DNS роутера.
2. Убедитесь, что у резолвера роутера есть рабочие upstream-серверы и он может до них достучаться.
3. Проверьте с роутера: `nslookup google.com 127.0.0.1`.
4. Если запись в `dns.servers` использует `detour`, проверьте выбранный outbound.

## Веб-сайты не открываются: `DNS_PROBE_FINISHED_BAD_CONFIG`

Обычно это означает, что резолвер не запущен или не смог применить конфигурацию. Проверьте сервис и журнал резолвера на вашей платформе (например Entware `dnsmasq` на Keenetic, `dnsmasq` на OpenWrt и Debian) и причины capability для `intercept` в `/api/health/service`.

## Firewall и `KeenPbrTable`

Если DNS работает и домен резолвится, но трафик всё равно идёт мимо VPN, проверьте firewall. `keen-pbr` создаёт изолированную цепочку или таблицу `KeenPbrTable`; трафик должен попадать туда, сопоставляться со списками и получать нужный `fwmark`.

Сначала выполните общий self-check:

```bash {filename="bash"}
keen-pbr status
```

Ищите проверки firewall со статусом `missing`, `mismatch` или `ERROR`.

### Проверка правил firewall

{{< tabs >}}
{{< tab name="iptables / ipset" selected=true >}}
```bash {filename="bash"}
iptables-save | grep KeenPbrTable
ip6tables-save | grep KeenPbrTable
```

Ожидаемо, есть переход из `PREROUTING` в `KeenPbrTable` и правила маркировки пакетов. Пример признака корректного правила:

```text
-A KeenPbrTable -m set --match-set <set> dst -j MARK --set-xmark <mark>/<mask>
```

Чтобы проверить наполнение set:

```bash {filename="bash"}
ipset list
ipset test <название_сета> <IP-адрес>
```

Ожидаемый ответ для совпадения: IP находится в указанном set.
{{< /tab >}}
{{< tab name="nftables / nftset" >}}
```bash {filename="bash"}
nft -t list ruleset
```

Если нужен полный вывод вместе с содержимым sets:

```bash {filename="bash"}
nft list ruleset
```

Ожидаемо, есть таблица `inet KeenPbrTable`, hook `prerouting`, правила сопоставления с sets и действие `meta mark set ...`.
{{< /tab >}}
{{< tab name="Debian" >}}
Backend зависит от того, как установлен firewall на вашей системе. Проверьте оба варианта или тот, который указан в логах `keen-pbr`.

```bash {filename="bash"}
iptables-save | grep KeenPbrTable
ip6tables-save | grep KeenPbrTable
nft -t list ruleset
```
{{< /tab >}}
{{< /tabs >}}

Если `KeenPbrTable` отсутствует, вернитесь к логам `keen-pbr`: обычно причина в недоступном backend, правах, отсутствующих пакетах или ошибке конфигурации.

## Таблицы маршрутизации и `fwmark`

Если firewall маркирует пакеты, но сайт бесконечно грузится или открывается через провайдера, проверьте policy routing. ОС должна увидеть `fwmark`, применить `ip rule` и отправить пакет в таблицу маршрутизации нужного outbound.

1. Проверьте состояние, которое ожидает `keen-pbr`:
   ```bash {filename="bash"}
   keen-pbr status
   ```
   Ищите строки со статусами `missing`, `mismatch` или `ERROR`.

2. Проверьте конкретный домен или IP:
   ```bash {filename="bash"}
   keen-pbr test-routing google.com
   ```
   Ожидаемо, expected и actual outbound совпадают.

3. Проверьте policy rules:
   ```bash {filename="bash"}
   ip rule show
   ```
   Ожидаемо, есть правило вида `fwmark <mark> lookup <table>`.

4. Проверьте таблицу маршрутизации:
   ```bash {filename="bash"}
   ip route show table <номер_таблицы>
   ```
   Ожидаемо, в таблице есть маршрут через нужный VPN-интерфейс или gateway. Для blackhole outbound ожидаемым результатом будет blackhole route.

Если таблица пустая, интерфейс не найден или правило ведёт не туда, проверьте имя outbound, имя интерфейса и конфликты `fwmark` / `iproute.table_start`.

## Маршрутизация ломается при включённом Tailscale

Tailscale резервирует `0x00FF0000`, что пересекается с маской keen-pbr по
умолчанию, и восстанавливает mark соединения с полной заменой packet mark.
Перенос keen-pbr в другой диапазон не мешает Tailscale стереть его mark.

Отключите netfilter-интеграцию Tailscale:

```bash {filename="bash"}
tailscale set --netfilter-mode=off
```

После этого правила firewall и NAT нужно настраивать отдельно, особенно если
Tailscale работает как subnet router или exit node.

## Интерфейсы и VPN-туннели

Если DNS, firewall и policy routing выглядят правильно, проверьте, что сам интерфейс существует, поднят и может отправлять трафик.

1. Откройте `http://<ip-роутера>:12121/` и проверьте runtime-состояние outbounds и интерфейсов.
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

Ожидаемо, `curl` возвращает внешний IP VPN. Если команда зависает или завершается ошибкой, проблема ниже `keen-pbr`: туннель не поднят, нет маршрута, недоступен gateway или блокируется исходящий трафик.

{{% details title="Если используется urltest или fallback" closed="true" %}}
1. Проверьте, что дочерние outbounds имеют корректные интерфейсы или таблицы.
2. Проверьте `GET /api/health/service` и runtime-состояние outbounds в Web UI.
3. Если circuit breaker находится в состоянии `"open"`, дождитесь истечения `circuit_breaker.timeout_ms` или исправьте недоступный child outbound.
4. Если резервный интерфейс не включается, сначала проверьте доступность каждого child outbound отдельно через `curl --interface`.
{{% /details %}}

## Удалённые списки не обновляются

1. Выполните на роутере или сервере:
   ```bash {filename="bash"}
   keen-pbr download
   ```
2. Если список всё ещё не обновляется, проверьте, достижим ли URL с этой же системы.
3. Если список должен скачиваться через VPN, проверьте `lists[].detour` и соответствующий outbound.
4. Если используется автоматическое обновление, проверьте `lists_autoupdate.cron`.
5. После ошибки снова прочитайте логи `keen-pbr`.

{{% details title="Расширенные проверки" closed="true" %}}
Если нужно принудительно выполнить полную перезагрузку:

```bash {filename="bash"}
kill -HUP $(cat /var/run/keen-pbr.pid)
```

Если в конфигурации задан другой `daemon.pid_file`, используйте его путь. Также подтвердите, что `daemon.cache_dir` доступен для записи.
{{% /details %}}

## `urltest` всегда показывает degraded

1. Убедитесь, что тестовый `url` достижим и возвращает хороший HTTP-ответ, например `200 OK` или `204 No Content`.
2. Для пользовательских проверок настоятельно рекомендуется использовать HTTP-адреса вместо HTTPS. HTTPS-проверки могут работать нестабильно из-за устаревших сертификатов, неполной цепочки доверия или особенностей TLS на роутере. Успешным для `urltest` считается финальный HTTP-код `2xx`.
3. Рекомендуемый адрес по умолчанию: `https://www.gstatic.com/generate_204`.
4. Убедитесь, что дочерние outbounds работают по отдельности.
5. Проверьте интерфейсы через `curl --interface <имя_интерфейса> https://ifconfig.co/json`.
6. Дождитесь следующего цикла проверки или временно уменьшите `interval_ms` во время диагностики.
7. Проверьте `GET /api/health/service` на состояние circuit breaker. Если child outbound находится в состоянии `"open"`, дождитесь `circuit_breaker.timeout_ms`.

## Правила фильтра портов/адресов не сопоставляются

{{< callout type="warning" >}}
Если вы используете отрицание в полях `src_addr` / `dest_addr`, отрицание применяется сразу ко всем указанным в этом поле IP/подсетям. Смешивание записей с отрицанием и без отрицания в одном списке невозможно. Как альтернатива, вы можете создать два отдельных правила.

Это же применимо к `src_port` / `dest_port`.
{{< /callout >}}

Если правила не сопоставляются как ожидается:

- Убедитесь, что `proto` установлен корректно: `null` для любых протоколов, `"tcp"`, `"udp"` или `"tcp/udp"`.
- Проверьте, что имя списка в правиле точно совпадает с ключом в `lists`, включая регистр.
- Запустите `keen-pbr test-routing <domain-or-ip>` и сравните expected / actual.
- Проверьте `keen-pbr status`, чтобы увидеть, созданы ли firewall rules для этого правила.

## Конфликты низкоуровневой маршрутизации

Если вы изменили настройки `fwmark` или `iproute`, или другой инструмент управляет policy routing на той же системе, пакеты могут быть неправильно направлены или отброшены.

Проверьте на конфликты:

```bash {filename="bash"}
keen-pbr status
ip rule show
ip route show table all
```

Для firewall marks:

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

`mask` должна состоять из одного или нескольких смежных hex-нибблов `F` и быть выровнена по границе nibble. Используйте hex-строки, например `"0x00FF0000"` и `"0x00020000"`.
