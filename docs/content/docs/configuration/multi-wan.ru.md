---
title: Балансировка нагрузки между несколькими WAN
weight: 7
---

Эта страница описывает, как распределять новые соединения между двумя и более интернет-подключениями (multi-WAN) на хосте Debian или Ubuntu с firewall-бэкендом `iptables`.

{{< callout type="warning" >}}
Балансировка нагрузки **недоступна на Keenetic**: сборка для Keenetic её не поддерживает и не примет конфигурацию с `"strategy": "balance"`. На этих устройствах используйте собственный multipath роутера. Бэкенд `nftables` (OpenWrt 24+ с fw4 или любой хост с `nft`) тоже поддерживает балансировку, см. [Outbounds]({{< relref "/docs/configuration/outbounds#urltest" >}}). Пошаговое руководство ниже рассчитано на iptables в Debian/Ubuntu, а заметки про {{< term "NAT" >}}, `rp_filter` и {{< term "fwmark" >}} применимы к любому бэкенду.
{{< /callout >}}

## Как это работает

Outbound `urltest` или `icmptest` с `"strategy": "balance"` выбирает один из рабочих каналов для каждого **нового** соединения пропорционально их `weight` (от 1 до 100, по умолчанию 1). Выбор сохраняется в {{< term "conntrack" >}} (в метке соединения, {{< term "connmark" >}}), поэтому всё соединение идёт через один WAN. keen-pbr только ставит пакетам **fwmark** в таблице {{< term "mangle" >}} и добавляет записи `ip rule` для этих {{< term "fwmark" >}}. Он никогда не меняет исходные адреса, поэтому {{< term "NAT" >}} настраиваете вы.

## Выбор бэкенда iptables

При `"firewall_backend": "auto"` (по умолчанию) keen-pbr использует nftables, если есть бинарный файл `nft`, и переключается на iptables в противном случае. Debian и Ubuntu по умолчанию устанавливают `nftables`, поэтому для использования iptables его нужно выбрать явно:

```json { filename="config.json" }
{
  "daemon": { "firewall_backend": "iptables" }
}
```

Допустимые значения: `auto`, `iptables` и `nftables`. Выбирайте iptables, если на хосте уже работает стек на основе iptables (Docker, ufw, fail2ban), чтобы все правила firewall находились в одном месте. Работают оба варианта: `iptables-nft` (по умолчанию в Debian) и `iptables-legacy` (`update-alternatives --config iptables`).

Балансировка на iptables использует match `statistic` (модуль ядра `xt_statistic`, входит в стандартное ядро Debian/Ubuntu). keen-pbr проверяет его один раз при запуске сервиса. Если модуль не работает, конфигурация с `"strategy": "balance"` отклоняется с ошибкой, в которой назван `xt_statistic`, а firewall остаётся без изменений. Балансировка на iptables также недоступна с `--use-raw-prerouting`.

{{< callout type="info" >}}
iptables выбирает WAN случайно с заданной вероятностью, поэтому на небольшом числе соединений распределение носит статистический характер. Поле `default_gateway` в правилах маршрутизации работает только с nftables, поэтому на iptables используйте явные правила, например `dest_addr` (см. пример ниже).
{{< /callout >}}

## Пример

Два подключения: `eth1` (30% новых соединений) и `eth2` (70%), LAN на `eth0`. Добавьте `"iproute": { "process_router_traffic": true }`, если соединения, инициированные самим хостом, тоже нужно балансировать (по умолчанию балансируется только транзитный трафик LAN).

```json { filename="/etc/keen-pbr/config.json" }
{
  "daemon": { "firewall_backend": "iptables" },
  "outbounds": [
    { "type": "interface", "tag": "wan1", "interface": "eth1", "gateway": "192.0.2.1" },
    { "type": "interface", "tag": "wan2", "interface": "eth2", "gateway": "198.51.100.1" },
    { "type": "ignore", "tag": "direct_local" },
    {
      "type": "urltest",
      "tag": "wan_balance",
      "url": "https://www.gstatic.com/generate_204",
      "interval_ms": 10000,
      "strategy": "balance",
      "outbound_groups": [
        {
          "members": [
            { "outbound": "wan1", "weight": 3 },
            { "outbound": "wan2", "weight": 7 }
          ]
        }
      ]
    }
  ],
  "lists": {
    "local_networks": {
      "ip_cidrs": [
        "0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8",
        "169.254.0.0/16", "172.16.0.0/12", "192.168.0.0/16",
        "224.0.0.0/4", "240.0.0.0/4"
      ]
    }
  },
  "route": {
    "inbound_interfaces": ["eth0"],
    "rules": [
      { "list": ["local_networks"], "outbound": "direct_local" },
      { "dest_addr": "0.0.0.0/0", "outbound": "wan_balance" }
    ]
  }
}
```

Используйте `icmptest` вместо `urltest`, если предпочитаете ICMP-проверки; тогда для каждого канала нужен адрес `target` (см. [Outbounds]({{< relref "/docs/configuration/outbounds#icmptest" >}})). Каналы, которые не прошли проверку, исключаются из распределения и возвращаются, когда снова заработают.

## Обязательная настройка системы

### 1. NAT на каждом WAN

{{< term "NAT" >}} **обязателен на каждом WAN-интерфейсе**. Без него соединения, отправленные на второй WAN, уходят с исходным адресом LAN, не получают ответа и зависают, хотя проверки работоспособности при этом проходят (они используют адрес интерфейса). Это касается и трафика самого хоста: его исходный адрес выбирается ещё до того, как keen-pbr отправит пакет в другой WAN.

```bash {filename="bash"}
sudo iptables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo iptables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
# IPv6, только если вы балансируете IPv6 и используете NAT66
sudo ip6tables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo ip6tables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
```

Сделайте правила постоянными, например с помощью `iptables-persistent` (`sudo apt install iptables-persistent`, затем `sudo netfilter-persistent save`), либо добавьте эквивалентные строки `*nat` в `/etc/ufw/before.rules`, если используете ufw. Если NAT на хосте управляется через nftables, добавьте `masquerade` для обоих интерфейсов в цепочку `postrouting` типа `nat`.

### 2. Обратный фильтр пути (reverse path filter)

Ответы на соединения через второй WAN приходят на интерфейс, через который ядро само не стало бы отправлять пакеты на этот адрес, и строгая проверка обратного пути (`rp_filter=1`) их отбрасывает. Значение по умолчанию в systemd — `2` (нестрогий режим), и оно работает. Проверьте и при необходимости исправьте:

```bash {filename="bash"}
sysctl net.ipv4.conf.all.rp_filter net.ipv4.conf.eth1.rp_filter net.ipv4.conf.eth2.rp_filter
printf 'net.ipv4.conf.all.rp_filter = 2\nnet.ipv4.conf.eth1.rp_filter = 2\nnet.ipv4.conf.eth2.rp_filter = 2\n' \
  | sudo tee /etc/sysctl.d/90-keen-pbr-multiwan.conf
sudo sysctl --system
```

Ядро использует большее из значений `all` и значения для конкретного интерфейса. keen-pbr не меняет `rp_filter`.

## fwmark, поставленные другими программами

По умолчанию `daemon.skip_marked_packets` равен `true`: любой пакет, который уже несёт ненулевой {{< term "fwmark" >}} (Docker, WireGuard `0xca6c`, Tailscale, скрипты {{< term "QoS" >}}), keen-pbr полностью пропускает, включая балансировку. Установите `false`, чтобы keen-pbr маршрутизировал и такие пакеты, но тогда он может перезаписать fwmark, на которые полагается другое ПО:

```json { filename="config.json" }
{ "daemon": { "skip_marked_packets": false } }
```

keen-pbr сохраняет свой выбор WAN в метке соединения ({{< term "connmark" >}}) в {{< term "conntrack" >}}, в пределах `fwmark.mask` (по умолчанию `0x00FF0000`; `fwmark.start` по умолчанию равен `0x00010000`, и каждый маршрутизируемый outbound использует два значения fwmark). Любая программа, которая пишет в те же биты, перезапишет выбор. Перенесите fwmark keen-pbr в биты, которые никто не использует. Маска должна состоять из идущих подряд целых шестнадцатеричных цифр `F` (например, `0xFF000000`):

```json { filename="config.json" }
{
  "fwmark": {
    "start": "0x01000000",
    "mask": "0xFF000000"
  }
}
```

Проверить, что уже используется, можно через `ip rule show` и `sudo iptables -t mangle -S | grep -i mark`. Если в маске не хватает значений fwmark для всех ваших outbound, keen-pbr не примет конфигурацию. Страница состояния (веб-интерфейс, `keen-pbr status`, `GET /api/health/routing`) предупреждает об отсутствующем {{< term "NAT" >}}, строгом `rp_filter`, а также о правилах firewall и `ip rule` других программ, которые используют биты внутри `fwmark.mask` (`fwmark_mask_conflict`). Это только предупреждения, применять конфигурацию они не мешают.

Tailscale — особый случай: перенос маски не поможет, см. [Маршрутизация ломается при включённом Tailscale]({{< relref "/docs/troubleshooting#tailscale" >}}).

## Известное ограничение: входящие соединения на втором WAN

Балансировка обрабатывает только соединения, которые открывают клиенты LAN (и, опционально, сам хост). Входящие соединения к хосту на втором WAN (SSH, проброс портов) отвечают через основной маршрут по умолчанию, и ответ уходит через неправильное подключение. keen-pbr пока это не обрабатывает. Обойти это можно вручную: ставьте входящим соединениям {{< term "connmark" >}} по интерфейсу через `CONNMARK`, восстанавливайте его в {{< term "fwmark" >}} ответов в `mangle OUTPUT` и добавьте `ip rule fwmark ... lookup <table>` для таблицы, у которой маршрут по умолчанию идёт через этот WAN. Используйте значения fwmark вне `fwmark.mask`.

## Устранение неполадок

| Симптом | Вероятная причина и проверка |
|---|---|
| Соединения зависают, когда идут через один WAN, проверки работоспособности в порядке | Отсутствует {{< term "NAT" >}} на этом WAN: `sudo iptables -t nat -S POSTROUTING` должен показывать `MASQUERADE` для каждого WAN. |
| Пакеты уходят, но ответы не приходят | Строгий `rp_filter`: действующее значение (`all` или для интерфейса, побеждает большее) должно быть `2` или `0`. |
| Весь трафик идёт через один WAN | {{< term "fwmark" >}} от других программ (`skip_marked_packets`), конфликт маски fwmark (страница состояния покажет предупреждение `fwmark_mask_conflict` с указанием правила) или второй WAN нездоров. Проверьте `sudo iptables -t mangle -S` и статус outbound в веб-интерфейсе или API. |
| Конфигурация отклонена с сообщением о `xt_statistic` | `sudo modprobe xt_statistic`, затем перезапустите сервис. |
| Нет правил iptables, вместо них появляются правила `nft` | `firewall_backend` равен `auto`, и `nft` установлен: установите `iptables`. |

Логи пишутся в syslog (в Debian — в systemd journal). Для более подробного вывода используйте `--log-level verbose` или `debug`, см. [CLI]({{< relref "/docs/cli" >}}).
