---
title: Балансировка нагрузки между несколькими WAN
weight: 7
---

Эта страница описывает, как распределять новые соединения между двумя и более интернет-подключениями (multi-WAN) на хосте Debian или Ubuntu с firewall-бэкендом `iptables`.

{{< callout type="warning" >}}
Балансировка нагрузки **недоступна на Keenetic**: она исключена при сборке, а валидация конфигурации отклоняет `"strategy": "balance"`. На этих устройствах используйте собственный multipath роутера. Бэкенд `nftables` (OpenWrt 24+ с fw4 или любой хост с `nft`) тоже поддерживает балансировку, см. [Outbounds]({{< relref "/docs/configuration/outbounds#urltest" >}}). Пошаговое руководство ниже рассчитано на iptables в Debian/Ubuntu, а заметки про NAT, `rp_filter` и fwmark применимы к любому бэкенду.
{{< /callout >}}

## Как это работает

Outbound `urltest` или `icmptest` с `"strategy": "balance"` выбирает один из пригодных членов для каждого **нового** соединения пропорционально их `weight` (от 1 до 100, по умолчанию 1). Выбор сохраняется в conntrack mark, поэтому остальная часть соединения остаётся на том же WAN. keen-pbr только **помечает** пакеты в таблице mangle и добавляет правила маршрутизации для меток. Он никогда не меняет исходные адреса, поэтому NAT настраиваете вы.

## Выбор бэкенда iptables

При `"firewall_backend": "auto"` (по умолчанию) keen-pbr использует nftables, если есть бинарный файл `nft`, и переключается на iptables в противном случае. Debian и Ubuntu по умолчанию устанавливают `nftables`, поэтому для использования iptables его нужно выбрать явно:

```json { filename="config.json" }
{
  "daemon": { "firewall_backend": "iptables" }
}
```

Допустимые значения: `auto`, `iptables` и `nftables`. Выбирайте iptables, если на хосте уже работает стек на основе iptables (Docker, ufw, fail2ban), чтобы все правила firewall находились в одном месте. Работают оба варианта: `iptables-nft` (по умолчанию в Debian) и `iptables-legacy` (`update-alternatives --config iptables`).

Балансировка на iptables использует match `statistic` (модуль ядра `xt_statistic`, входит в стандартное ядро Debian/Ubuntu). Его доступность проверяется один раз при запуске сервиса. Если модуль не работает, keen-pbr отказывается применять конфигурацию с balance outbound и сообщает об ошибке с именем `xt_statistic` до каких-либо изменений firewall. Балансировка на iptables также недоступна с `--use-raw-prerouting`.

{{< callout type="info" >}}
iptables выбирает WAN случайно с заданной вероятностью, поэтому на небольшом числе соединений распределение носит статистический характер. Матчер правил маршрутизации `default_gateway` работает только в nftables, поэтому на iptables используйте явные правила, например `dest_addr` (см. пример ниже).
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

Используйте `icmptest` вместо `urltest`, если предпочитаете ICMP-проверки; тогда каждому члену нужен явный адрес `target` (см. [Outbounds]({{< relref "/docs/configuration/outbounds#icmptest" >}})). Члены, которые не прошли проверку работоспособности, исключаются из распределения и возвращаются, когда восстанавливаются.

## Обязательная настройка системы

### 1. NAT на каждом WAN

NAT **обязателен на каждом WAN-интерфейсе**. Без него соединения, отправленные на второй WAN, уходят с исходным адресом LAN, не получают ответа и зависают, хотя проверки работоспособности при этом проходят (они используют адрес интерфейса). То же относится к трафику, инициированному самим хостом, поскольку исходный адрес выбирается до того, как пакет переотправляется по другому маршруту.

```bash {filename="bash"}
sudo iptables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo iptables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
# IPv6, только если вы балансируете IPv6 и используете NAT66
sudo ip6tables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo ip6tables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
```

Сделайте правила постоянными, например с помощью `iptables-persistent` (`sudo apt install iptables-persistent`, затем `sudo netfilter-persistent save`), либо добавьте эквивалентные строки `*nat` в `/etc/ufw/before.rules`, если используете ufw. Если NAT на хосте управляется через nftables, добавьте `masquerade` для обоих интерфейсов в цепочку `postrouting` типа `nat`.

### 2. Обратный фильтр пути (reverse path filter)

Ответы на соединение, отправленное через второй WAN, могут прийти на интерфейс, который не является лучшим маршрутом обратно к источнику, поэтому строгий reverse path filtering (`rp_filter=1`) отбрасывает их. Значение по умолчанию в systemd — `2` (нестрогий режим), и оно работает. Проверьте и при необходимости исправьте:

```bash {filename="bash"}
sysctl net.ipv4.conf.all.rp_filter net.ipv4.conf.eth1.rp_filter net.ipv4.conf.eth2.rp_filter
printf 'net.ipv4.conf.all.rp_filter = 2\nnet.ipv4.conf.eth1.rp_filter = 2\nnet.ipv4.conf.eth2.rp_filter = 2\n' \
  | sudo tee /etc/sysctl.d/90-keen-pbr-multiwan.conf
sudo sysctl --system
```

Ядро использует большее из значений `all` и значения для конкретного интерфейса. keen-pbr не меняет `rp_filter`.

## Чужие метки пакетов

По умолчанию `daemon.skip_marked_packets` равен `true`: любой пакет, который уже несёт ненулевой fwmark (Docker, WireGuard `0xca6c`, Tailscale, скрипты QoS), keen-pbr полностью пропускает, включая балансировку. Установите `false`, чтобы keen-pbr маршрутизировал и такие пакеты, но тогда он может перезаписать метки, на которые полагается другое ПО:

```json { filename="config.json" }
{ "daemon": { "skip_marked_packets": false } }
```

keen-pbr сохраняет свой выбор WAN в conntrack mark в пределах `fwmark.mask` (по умолчанию `0x00FF0000`; `fwmark.start` по умолчанию равен `0x00010000`, и каждый маршрутизируемый outbound использует две метки). Любой сервис, который пишет в те же биты connmark, перезапишет выбор. Tailscale помечает пакеты значениями `0x40000` и `0x80000`, которые попадают внутрь маски по умолчанию. Переместите метки keen-pbr в биты, которые никто не использует. Маска должна быть выровнена по нибблам и состоять из подряд идущих нибблов `F`:

```json { filename="config.json" }
{
  "fwmark": {
    "start": "0x01000000",
    "mask": "0xFF000000"
  }
}
```

Проверить, что уже используется, можно через `ip rule show` и `sudo iptables -t mangle -S | grep -i mark`. Валидация конфигурации сообщит об ошибке, если в маске недостаточно меток для ваших outbound. Страница состояния (`GET /api/health/routing`, `keen-pbr status` и обзор в веб-интерфейсе) предупреждает об отсутствующем NAT, строгом `rp_filter` и `fwmark_mask_conflict`: сторонних правилах iptables `MARK`/`CONNMARK` (таблицы mangle и raw, только бэкенд iptables) или записях `ip rule`, которые записывают или сопоставляют биты внутри `fwmark.mask`. Эти предупреждения никогда не блокируют применение.

## Известное ограничение: входящие соединения на втором WAN

Балансировка обрабатывает только соединения, которые открывают клиенты LAN (и, опционально, сам хост). Входящие соединения к хосту на втором WAN (SSH, проброс портов) отвечают через основной маршрут по умолчанию, и ответ уходит через неправильное подключение. keen-pbr пока это не обрабатывает. Как ручной обходной путь: помечайте входящие соединения по интерфейсам с помощью `CONNMARK`, восстанавливайте метку для ответов в `mangle OUTPUT` и добавьте `ip rule fwmark ... lookup <table>` для таблицы, у которой маршрутом по умолчанию служит этот WAN, используя метку вне `fwmark.mask`.

## Устранение неполадок

| Симптом | Вероятная причина и проверка |
|---|---|
| Соединения зависают, когда идут через один WAN, проверки работоспособности в порядке | Отсутствует NAT на этом WAN: `sudo iptables -t nat -S POSTROUTING` должен показывать `MASQUERADE` для каждого WAN. |
| Пакеты уходят, но ответы не приходят | Строгий `rp_filter`: действующее значение (`all` или для интерфейса, побеждает большее) должно быть `2` или `0`. |
| Весь трафик идёт через один WAN | Чужие метки (`skip_marked_packets`), конфликт маски fwmark (страница состояния покажет предупреждение `fwmark_mask_conflict` с указанием правила) или второй WAN нездоров. Проверьте `sudo iptables -t mangle -S` и статус outbound в веб-интерфейсе или API. |
| Конфигурация отклонена с сообщением о `xt_statistic` | `sudo modprobe xt_statistic`, затем перезапустите сервис. |
| Нет правил iptables, вместо них появляются правила `nft` | `firewall_backend` равен `auto`, и `nft` установлен: установите `iptables`. |

Логи пишутся в syslog (в Debian — в systemd journal). Для более подробного вывода используйте `--log-level verbose` или `debug`, см. [CLI]({{< relref "/docs/cli" >}}).
