---
title: Устранение неполадок
weight: 7
aliases:
  - /docs/troubleshooting/troubleshooting/
---

Начните с логов и статуса сервисов, затем переходите к DNS, firewall, таблицам маршрутизации и интерфейсам. В `keen-pbr` 3.x демон активно пишет причины ошибок в системный журнал, поэтому логи почти всегда быстрее угадывания по симптомам.

## Быстрый порядок диагностики

1. Проверьте системный журнал на ошибки `keen-pbr` и `dnsmasq`.
2. Проверьте, что сервисы `keen-pbr` и `dnsmasq` запущены.
3. Если `keen-pbr` падает при запуске, запустите его вручную в foreground-режиме, чтобы увидеть больше логов: `keen-pbr --log-level verbose service`.
4. Проверьте DNS: устройство должно использовать DNS роутера, а `dnsmasq` должен отвечать локально.
5. Проверьте firewall: правила `keen-pbr` должны быть в `KeenPbrTable`.
6. Проверьте policy routing: `fwmark` должен вести в нужную таблицу маршрутизации.
7. Проверьте интерфейсы и VPN-туннели.
8. Если проблема остаётся, проверьте удалённые списки, `urltest`, фильтры правил и конфликты низкоуровневой маршрутизации.

## Системный журнал

Это первое место для диагностики. Ищите сообщения `keen-pbr`, `dnsmasq`, а также уровни `[E]` и `[W]`: так `keen-pbr` помечает ошибки и предупреждения. Если не хватает библиотек `iptables` / `nftables`, не найден интерфейс, занят порт API или сломан JSON, причина обычно будет написана именно здесь.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
В веб-интерфейсе Keenetic откройте **Диагностика** → **Системный журнал**.

Из консоли можно прочитать журнал так:

```bash {filename="bash"}
ndmc -c "show log once" | grep -E 'keen-pbr|dnsmasq|\[E\]|\[W\]|error|warn|warning'
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
logread | grep -E 'keen-pbr|dnsmasq|\[E\]|\[W\]|error|warn|warning'
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
journalctl -u keen-pbr -u dnsmasq
```

Если нужен только текущий запуск:

```bash {filename="bash"}
journalctl -u keen-pbr -u dnsmasq -b
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
4. Проверьте статус `dnsmasq`:
   ```bash {filename="bash"}
   /opt/etc/init.d/S56dnsmasq status
   ```
5. Прочитайте системный журнал:
   ```bash {filename="bash"}
   ndmc -c "show log once" | grep -E 'keen-pbr|dnsmasq|\[E\]|\[W\]|error|warn|warning'
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
4. Проверьте статус `dnsmasq`:
   ```bash {filename="bash"}
   service dnsmasq status
   ```
5. Прочитайте системный журнал:
   ```bash {filename="bash"}
   logread | grep -E 'keen-pbr|dnsmasq|\[E\]|\[W\]|error|warn|warning'
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
4. Проверьте статус `dnsmasq`:
   ```bash {filename="bash"}
   systemctl status dnsmasq
   ```
5. Прочитайте системный журнал:
   ```bash {filename="bash"}
   journalctl -u keen-pbr -u dnsmasq -b
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
   - Откройте `http://<ip-роутера>:12121/` и посмотрите на виджет проверки DNS. Там должно быть сказано "DNS-запрос из браузера достиг dnsmasq".
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

## DNS и dnsmasq

DNS должен пройти всю цепочку: клиент использует DNS роутера, `dnsmasq` запущен, сгенерированный конфиг `keen-pbr` подключён, домены попадают в `ipset` или `nftset`, а обычные домены уходят в `dns.fallback`.

### Проверка DNS с устройства пользователя

Откройте `http://<ip-роутера>:12121/` и проверьте DNS Check. Если Web UI недоступен, выполните с клиентского устройства:

```bash {filename="bash"}
nslookup check.keen.pbr
```

Ожидаемый ответ: `127.0.0.88`. Если ответа нет, устройство не использует DNS роутера или `dnsmasq` не отвечает.

### Проверка dnsmasq на роутере или сервере

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
/opt/etc/init.d/S56dnsmasq status
nslookup google.com 127.0.0.1
```

Если DNS-запросы клиентов не доходят до Entware `dnsmasq`, проверьте уникальную для Keenetic настройку:

```bash {filename="bash"}
opkg dns-override
```

После изменения сохраните конфигурацию Keenetic:

```bash {filename="bash"}
system configuration save
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
service dnsmasq status
nslookup google.com 127.0.0.1
```

Для domain-based routing нужен `dnsmasq-full`. Если в логах есть ошибки про неподдерживаемые `ipset` / `nftset`, проверьте установленный пакет:

```bash {filename="bash"}
opkg list-installed | grep dnsmasq
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
systemctl status dnsmasq
nslookup google.com 127.0.0.1
```

Если `systemctl` недоступен, используйте:

```bash {filename="bash"}
service dnsmasq status
```
{{< /tab >}}
{{< /tabs >}}

Ожидаемо, `nslookup <домен> 127.0.0.1` возвращает IP-адреса. Если видите `Connection refused`, `dnsmasq` не запущен или слушает не на `127.0.0.1:53`.

### Проверка сгенерированного resolver config

Выберите backend, который используется на вашей системе.

{{< tabs >}}
{{< tab name="iptables / ipset" selected=true >}}
```bash {filename="bash"}
keen-pbr generate-resolver-config dnsmasq-ipset
```

Ожидаемо, вывод содержит директивы вида `ipset=/example.com/<set>`.
{{< /tab >}}
{{< tab name="nftables / nftset" >}}
```bash {filename="bash"}
keen-pbr generate-resolver-config dnsmasq-nftset
```

Ожидаемо, вывод содержит директивы вида `nftset=/example.com/...`.
{{< /tab >}}
{{< /tabs >}}

Команда не должна завершаться ошибкой. Если она пишет, что кэш удалённого списка отсутствует, выполните:

```bash {filename="bash"}
keen-pbr download
```

{{% details title="Если DNS-правила не работают" closed="true" %}}
1. Убедитесь, что имя списка в `dns.rules` точно совпадает с именем списка в `lists`.
2. Убедитесь, что DNS-правило указывает на правильный тег DNS-сервера.
3. Если DNS-сервер использует `detour`, убедитесь, что выбранный outbound работает.
4. Убедитесь, что конфигурация `dnsmasq` подключает generated config через `conf-file=` или `conf-script=`.
5. Перезапустите `keen-pbr` и `dnsmasq`, затем снова проверьте логи.
{{% /details %}}

## Веб-сайты не открываются: `DNS_PROBE_FINISHED_NXDOMAIN` / `ERR_NAME_NOT_RESOLVED`

1. Убедитесь, что `dns.fallback` настроен и указывает как минимум на один рабочий тег DNS-сервера.
2. Убедитесь, что fallback DNS-сервер достижим с роутера или сервера. Если этот DNS-сервер использует `detour`, проверьте выбранный outbound.
3. Убедитесь, что устройство пользователя использует DNS роутера.
4. Перезапустите `keen-pbr` после изменения DNS-конфигурации.

Пример:

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "default_dns",
        "address": "1.1.1.1"
      }
    ],
    "fallback": ["default_dns"]
  }
}
```

Без `dns.fallback` домены, которые не соответствуют ни одной записи `dns.rules`, могут не разрешиться.

## Веб-сайты не открываются: `DNS_PROBE_FINISHED_BAD_CONFIG`

Это обычно означает, что `dnsmasq` не запущен или не смог применить свою конфигурацию.

1. Проверьте логи `dnsmasq`.
2. Проверьте статус `dnsmasq`.
3. Если вы недавно меняли DNS-настройки, перезапустите `keen-pbr` и `dnsmasq`.

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
ndmc -c "show log once" | grep dnsmasq
/opt/etc/init.d/S56dnsmasq status
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
logread | grep dnsmasq
service dnsmasq status
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
journalctl -u dnsmasq -b
systemctl status dnsmasq
```
{{< /tab >}}
{{< /tabs >}}

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
