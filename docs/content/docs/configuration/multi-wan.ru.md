---
title: Балансировка нагрузки между несколькими WAN
weight: 7
---

Эта страница для тех, у кого к хосту на Debian или Ubuntu подключены два интернет-провайдера и новые соединения нужно распределить между ними (multi-WAN). Описан firewall-бэкенд `iptables`.

{{< callout type="warning" >}}
Балансировка нагрузки **недоступна на Keenetic**: сборка для Keenetic её не поддерживает и не примет конфигурацию с `"strategy": "balance"`. На этих устройствах используйте собственный multipath роутера. Бэкенд `nftables` (OpenWrt 24+ с fw4 или любой хост с `nft`) тоже поддерживает балансировку, см. [Outbounds]({{< relref "/docs/configuration/outbounds#urltest" >}}). Пошаговое руководство ниже рассчитано на iptables в Debian/Ubuntu, а заметки про {{< term "NAT" >}}, `rp_filter` и {{< term "fwmark" >}} (числовую метку на пакете) применимы к любому бэкенду.
{{< /callout >}}

## Как это работает

Outbound `urltest` или `icmptest` с `"strategy": "balance"` выбирает один из рабочих каналов для каждого **нового** соединения пропорционально их `weight` (от 1 до 100, по умолчанию 1). Всё соединение идёт через один канал. Каналы, которые не прошли проверку, исключаются из распределения и возвращаются, когда снова заработают.

Две вещи в системе вы настраиваете сами: {{< term "NAT" >}} на каждом провайдере и `rp_filter` (см. ниже).

## Выбор бэкенда iptables

При `"firewall_backend": "auto"` (по умолчанию) keen-pbr использует nftables, если есть бинарный файл `nft`, и переключается на iptables в противном случае. Debian и Ubuntu по умолчанию устанавливают `nftables`, поэтому для использования iptables его нужно выбрать явно:

```json { filename="config.json" }
{
  "daemon": { "firewall_backend": "iptables" }
}
```

Допустимые значения: `auto`, `iptables` и `nftables`. Выбирайте iptables, если на хосте уже работает стек на основе iptables (Docker, ufw, fail2ban), чтобы все правила firewall находились в одном месте. Работают оба варианта: `iptables-nft` (по умолчанию в Debian) и `iptables-legacy` (`update-alternatives --config iptables`).

Балансировка на iptables использует модуль ядра `xt_statistic`, который входит в стандартное ядро Debian/Ubuntu. keen-pbr проверяет его один раз при запуске сервиса. Если модуль не работает, конфигурация с `"strategy": "balance"` отклоняется с ошибкой, в которой назван `xt_statistic`, а firewall остаётся без изменений. Балансировка на iptables также недоступна с `--use-raw-prerouting`.

{{< callout type="info" >}}
iptables выбирает канал случайно с заданной вероятностью, поэтому на небольшом числе соединений распределение получается приблизительным. Поле `default_gateway` в правилах маршрутизации работает только с nftables, поэтому на iptables используйте явные правила, например `dest_addr` (см. пример ниже).
{{< /callout >}}

## Пример

Два подключения: `eth1` (30% новых соединений) и `eth2` (70%), LAN на `eth0`. Если соединения самого хоста тоже нужно балансировать, включите **Общее** > **Настройки** > **Общие** > **Обрабатывать собственный трафик роутера** (виден в режиме настроек **Продвинутый**) (`iproute.process_router_traffic`); по умолчанию балансируется только транзитный трафик LAN. Интерфейсы LAN задаются в поле **Обрабатываемые (входящие) интерфейсы** (`route.inbound_interfaces`).

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

Используйте `icmptest` вместо `urltest`, если предпочитаете ICMP-проверки; тогда для каждого канала нужен адрес `target` (см. [Outbounds]({{< relref "/docs/configuration/outbounds#icmptest" >}})).

## Обязательная настройка системы

### 1. NAT на каждом WAN

{{< term "NAT" >}} **обязателен на каждом WAN-интерфейсе**. Без него пакет уходит ко второму провайдеру с адресом из вашей локальной сети, ответ на него не возвращается, и соединение зависает. Проверки работоспособности при этом проходят, потому что используют адрес самого интерфейса. Это касается и трафика самого хоста.

```bash {filename="bash"}
sudo iptables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo iptables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
# IPv6, только если вы балансируете IPv6 и используете NAT66
sudo ip6tables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo ip6tables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
```

Сделайте правила постоянными, например с помощью `iptables-persistent` (`sudo apt install iptables-persistent`, затем `sudo netfilter-persistent save`), либо добавьте эквивалентные строки `*nat` в `/etc/ufw/before.rules`, если используете ufw. Если NAT на хосте управляется через nftables, добавьте `masquerade` для обоих интерфейсов в цепочку `postrouting` типа `nat`.

### 2. Обратный фильтр пути (rp_filter)

Строгий фильтр ядра (`rp_filter=1`) выбрасывает ответ, который пришёл не через тот интерфейс, через который ядро само отправило бы пакет этому адресу. При двух провайдерах ответы второго как раз такие. Значение по умолчанию в systemd — `2` (нестрогий режим), и оно работает. Проверьте и при необходимости исправьте:

```bash {filename="bash"}
sysctl net.ipv4.conf.all.rp_filter net.ipv4.conf.eth1.rp_filter net.ipv4.conf.eth2.rp_filter
printf 'net.ipv4.conf.all.rp_filter = 2\nnet.ipv4.conf.eth1.rp_filter = 2\nnet.ipv4.conf.eth2.rp_filter = 2\n' \
  | sudo tee /etc/sysctl.d/90-keen-pbr-multiwan.conf
sudo sysctl --system
```

Ядро использует большее из значений `all` и значения для конкретного интерфейса.

## Метки (fwmark), поставленные другими программами

Метки на пакетах ставят не только keen-pbr, но и Docker, WireGuard (`0xca6c`), Tailscale, скрипты {{< term "QoS" >}}. По умолчанию keen-pbr полностью пропускает пакет, у которого уже есть чужая метка, включая балансировку. В веб-интерфейсе это переключатель **Общее** > **Настройки** > **Общие** > **Не обрабатывать маркированные пакеты** (`daemon.skip_marked_packets`). Если выключить его, keen-pbr будет маршрутизировать и такие пакеты, но может перезаписать метки, на которые полагается другое ПО:

```json { filename="config.json" }
{ "daemon": { "skip_marked_packets": false } }
```

Если другая программа пишет метки в те же биты, что и keen-pbr, выбор канала сбивается. Тогда перенесите метки keen-pbr в биты, которые никто не использует. По умолчанию это `fwmark.mask` равный `0x00FF0000` и `fwmark.start` равный `0x00010000`; каждому маршрутизируемому outbound нужно два значения. Маска должна состоять из идущих подряд целых шестнадцатеричных цифр `F` (например, `0xFF000000`):

```json { filename="config.json" }
{
  "fwmark": {
    "start": "0x01000000",
    "mask": "0xFF000000"
  }
}
```

Проверить, что уже используется, можно через `ip rule show` и `sudo iptables -t mangle -S | grep -i mark`. Если в маске не хватает значений для всех ваших outbound, keen-pbr не примет конфигурацию. Страница состояния (веб-интерфейс, `keen-pbr status`, `GET /api/health/routing`) предупреждает об отсутствующем {{< term "NAT" >}}, строгом `rp_filter`, а также о правилах firewall и `ip rule` других программ, которые используют биты внутри `fwmark.mask` (`fwmark_mask_conflict`). Это только предупреждения, применять конфигурацию они не мешают.

Tailscale — особый случай: перенос маски не поможет, см. [Маршрутизация ломается при включённом Tailscale]({{< relref "/docs/troubleshooting#tailscale" >}}).

## Известное ограничение: входящие соединения на втором WAN

Балансировка работает только для соединений, которые открывают устройства вашей сети (и, если включено, сам хост). Входящие соединения ко второму провайдеру (SSH, проброс портов) получают ответ через основной маршрут по умолчанию, то есть через неправильное подключение. keen-pbr пока это не обрабатывает. Обойти это можно вручную: ставьте входящим соединениям {{< term "connmark" >}} по интерфейсу через `CONNMARK`, восстанавливайте его в {{< term "fwmark" >}} ответов в `mangle OUTPUT` и добавьте `ip rule fwmark ... lookup <table>` для таблицы, у которой маршрут по умолчанию идёт через этот WAN. Используйте значения fwmark вне `fwmark.mask`.

## Устранение неполадок

| Симптом | Вероятная причина и проверка |
|---|---|
| Соединения зависают, когда идут через один WAN, проверки работоспособности в порядке | Отсутствует {{< term "NAT" >}} на этом WAN: `sudo iptables -t nat -S POSTROUTING` должен показывать `MASQUERADE` для каждого WAN. |
| Пакеты уходят, но ответы не приходят | Строгий `rp_filter`: действующее значение (`all` или для интерфейса, побеждает большее) должно быть `2` или `0`. |
| Весь трафик идёт через один WAN | Метки от других программ (**Не обрабатывать маркированные пакеты**), конфликт маски fwmark (страница состояния покажет предупреждение `fwmark_mask_conflict` с указанием правила) или второй WAN нездоров. Проверьте `sudo iptables -t mangle -S` и статус outbound в веб-интерфейсе или API. |
| Конфигурация отклонена с сообщением о `xt_statistic` | `sudo modprobe xt_statistic`, затем перезапустите сервис. |
| Нет правил iptables, вместо них появляются правила `nft` | `firewall_backend` равен `auto`, и `nft` установлен: установите `iptables`. |

Логи пишутся в syslog (в Debian — в systemd journal). Для более подробного вывода используйте `--log-level verbose` или `debug`, см. [CLI]({{< relref "/docs/cli" >}}).
