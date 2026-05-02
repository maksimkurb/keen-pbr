---
title: Конфигурация
weight: 3
---

keen-pbr хранит настройки в едином JSON-файле конфигурации.

Расположение файла конфигурации:

- Keenetic / NetCraze: `/opt/etc/keen-pbr/config.json`
- OpenWrt: `/etc/keen-pbr/config.json`
- Debian: `/etc/keen-pbr/config.json`

Если установлен полный пакет, первоначальную настройку удобнее сделать через веб-интерфейс и вернуться к этому разделу позже. Большинству пользователей достаточно четырёх разделов конфигурации:

- [Outbounds](outbounds/) — куда должен идти трафик
- [Списки](lists/) — сайты или диапазоны IP для сопоставления
- [Правила маршрутизации](route-rules/) — какие списки через какой outbound
- [DNS](dns/) — какой DNS-сервер использовать для этих списков

## Практический пример

В этом примере `google.com` идёт через `vpn`, а весь остальной трафик — через `wan`:

```json
{
  "outbounds": [
    {
      "type": "interface",
      "tag": "vpn",
      "interface": "tun0",
      "gateway": "10.8.0.1",
      "gateway6": "2001:db8::1"
    },
    {
      "type": "interface",
      "tag": "wan",
      "interface": "eth0",
      "gateway": "192.168.1.1"
    }
  ],
  "lists": {
    "my_sites": {
      "domains": ["google.com"]
    }
  },
  "dns": {
    "system_resolver": {
      "type": "dnsmasq-nftset",
      "address": "127.0.0.1"
    },
    "servers": [
      {
        "tag": "vpn_dns",
        "address": "10.8.0.1",
        "detour": "vpn"
      },
      {
        "tag": "default_dns",
        "address": "1.1.1.1"
      }
    ],
    "rules": [
      {
        "list": ["my_sites"],
        "server": "vpn_dns"
      }
    ],
    "fallback": ["default_dns"]
  },
  "route": {
    "rules": [
      {
        "list": ["my_sites"],
        "outbound": "vpn"
      }
    ]
  }
}
```

## Базовая конфигурация

- [Outbounds](outbounds/) — задайте VPN и обычное интернет-соединение
- [Списки](lists/) — задайте сайты, домены или диапазоны IP для сопоставления
- [Правила маршрутизации](route-rules/) — свяжите каждый список с outbound
- [DNS](dns/) — настройте DNS-сервер для доменов из списков

## Расширенная конфигурация

Для большинства пользователей эти настройки необязательны:

- [Расширенные](advanced/) — API, пути сервиса, автоматическое обновление списков и низкоуровневые параметры маршрутизации
- [Полный пример конфигурации](full-reference-config/) — пример с комментариями для всех поддерживаемых опций

{{< callout type="info" >}}
Имена списков, теги outbound и теги DNS-серверов должны соответствовать шаблону `^[a-z][a-z0-9_]*$` и не превышать 24 символа.
{{< /callout >}}
