---
title: Балансировка нагрузки между несколькими WAN
weight: 7
---

Эта страница для роутера или хоста, к которому подключены два и более интернет-провайдера и новые соединения нужно распределить между ними (multi-WAN).

{{< callout type="warning" >}}
Балансировка нагрузки **недоступна на Keenetic**: сборка для Keenetic её не поддерживает и не примет конфигурацию с `"strategy": "balance"`. На этих устройствах включите многопутевую передачу в профиле подключения.
{{< /callout >}}

## Как это работает

{{< only openwrt debian >}}\
Outbound `urltest` или `icmptest` с `"strategy": "balance"` выбирает один из рабочих каналов для каждого **нового** соединения пропорционально их `weight` (от 1 до 100, по умолчанию 1). Всё соединение идёт через один канал. Каналы, которые не прошли проверку, исключаются из распределения и возвращаются, когда снова заработают.

В системе вам нужно настроить {{< term "NAT" >}} на каждом провайдере и `rp_filter` (см. ниже).

{{< callout type="info" >}}
{{< only iptables >}} Каналы выбираются случайно с заданной вероятностью, поэтому на небольшом числе соединений доли получаются приблизительными. Бэкенду `iptables` нужен модуль ядра `xt_statistic`. Менять `firewall_backend` обычно не нужно: keen-pbr сам выбирает подходящий бэкенд.
{{< /callout >}}

## Пример

Два подключения: `eth1` (30% новых соединений) и `eth2` (70%), LAN на `eth0`. Если соединения самого хоста тоже нужно балансировать, включите **Общее** > **Настройки** > **Общие** > **Обрабатывать собственный трафик роутера** (виден в режиме настроек **Продвинутый**); по умолчанию балансируется только трафик устройств из LAN. Интерфейсы LAN задаются в поле **Обрабатываемые (входящие) интерфейсы**.

```json { filename="/etc/keen-pbr/config.json" }
{
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

{{< term "NAT" >}} **обязателен на каждом WAN-интерфейсе**. Без него пакет уходит ко второму провайдеру с адресом из вашей локальной сети, ответ не возвращается, и соединение зависает. Проверки каналов при этом проходят, поэтому проблему легко пропустить. Это касается и трафика самого хоста.

{{< only openwrt >}}\
В **Network** > **Firewall** > **Zones** включите **Masquerading** для каждой зоны, в которой есть подключение к провайдеру (по умолчанию это зона `wan`).

{{< only nftables >}}\
Добавьте `masquerade` для обоих интерфейсов, например в `/etc/nftables.conf`:

```nft {filename="/etc/nftables.conf"}
table inet multiwan_nat {
  chain postrouting {
    type nat hook postrouting priority srcnat; policy accept;
    oifname { "eth1", "eth2" } masquerade
  }
}
```

{{< only iptables >}}\
Добавьте правила `MASQUERADE` и сохраните их, например через `iptables-persistent`:

```bash {filename="bash"}
sudo iptables -t nat -A POSTROUTING -o eth1 -j MASQUERADE
sudo iptables -t nat -A POSTROUTING -o eth2 -j MASQUERADE
```

### 2. rp_filter

Строгая проверка обратного пути (`rp_filter=1`) отбрасывает ответы второго провайдера. Нужен нестрогий режим (`2`). Проверьте значения и при необходимости исправьте:

```bash {filename="bash"}
sysctl net.ipv4.conf.all.rp_filter net.ipv4.conf.eth1.rp_filter net.ipv4.conf.eth2.rp_filter
printf 'net.ipv4.conf.all.rp_filter = 2\nnet.ipv4.conf.eth1.rp_filter = 2\nnet.ipv4.conf.eth2.rp_filter = 2\n' \
  | sudo tee /etc/sysctl.d/90-keen-pbr-multiwan.conf
sudo sysctl --system
```

На OpenWrt добавьте эти же три строки в `/etc/sysctl.conf` и выполните `/etc/init.d/sysctl restart`.

## Если на роутере работают другие программы с метками

Метки на пакетах ставят не только keen-pbr, но и Docker, WireGuard, Tailscale, скрипты {{< term "QoS" >}} и mwan3 на OpenWrt. По умолчанию keen-pbr не трогает пакеты с чужой меткой, и они не балансируются. Это управляется переключателем **Общее** > **Настройки** > **Общие** > **Не обрабатывать маркированные пакеты**.

Если выбор канала сбивается из-за чужих меток, предупреждение `fwmark_mask_conflict` появится на странице состояния. Тогда перенесите метки keen-pbr в свободные биты:

```json { filename="config.json" }
{
  "fwmark": {
    "start": "0x01000000",
    "mask": "0xFF000000"
  }
}
```

С Tailscale это не поможет, см. [Маршрутизация ломается при включённом Tailscale]({{< relref "/docs/troubleshooting#tailscale" >}}).

## Ограничение: входящие соединения на втором WAN

Балансировка работает только для соединений, которые открывают устройства вашей сети (и сам хост, если включено). Входящие соединения ко второму провайдеру (SSH, проброс портов) получают ответ через основное подключение, то есть через другого провайдера, и могут не работать.

## Устранение неполадок

| Симптом | Что проверить |
|---|---|
| Часть соединений зависает, проверки каналов в порядке | {{< term "NAT" >}} включён на каждом WAN. |
| Пакеты уходят, но ответы не приходят | `rp_filter` равен `2` или `0` (побеждает большее из значений `all` и интерфейса). |
| Весь трафик идёт через один WAN | Второй WAN нездоров (статус на странице **Маршрутизация** > **Outbounds**) или метки других программ (см. выше). |
| Конфигурация отклонена с сообщением о `xt_statistic` | `sudo modprobe xt_statistic`, затем перезапустите сервис. |

Логи пишутся в syslog (на OpenWrt — `logread`). Для более подробного вывода используйте `--log-level verbose` или `debug`, см. [CLI]({{< relref "/docs/cli" >}}).
