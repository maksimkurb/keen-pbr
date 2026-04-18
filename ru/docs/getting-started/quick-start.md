# Быстрый старт

Это руководство поможет завершить первую рабочую настройку, которая отправляет выбранные сайты через VPN, сохраняя обычное соединение для остального трафика.

Если вы установили полный пакет, веб-интерфейс — самое простое место для начала. Если вы установили `keen-pbr-headless` или предпочитаете редактировать файлы напрямую, используйте вкладку JSON / CLI.

{{< tabs >}}
{{< tab name="Веб-интерфейс" selected=true >}}

{{< callout type="info" >}}
Если вы установили `keen-pbr-headless`, см. метод конфигурации **JSON**.
{{< /callout >}}

<p>
    <figure class="video">
        <video
        controls
        playsinline
        preload="metadata"
        style="width: 100%; border-radius: 12px;"
        >
        <source src="/docs/getting-started/quick-start-video.mp4" type="video/mp4">
        Your browser does not support the video tag.
        </video>
    </figure>
</p>

{{% steps %}}
### Откройте веб-интерфейс

Откройте `http://<ip-роутера>:12121/` в браузере. На Keenetic / NetCraze также можно открыть `http://my.keenetic.net:12121/`.

### Создайте outbounds

Перейдите в **Outbounds** и создайте две записи:

1. Создайте outbound с именем `vpn` со следующими параметрами:
    - `тип = Интерфейс`
    - `интерфейс = <имя-вашего-vpn-интерфейса>`
    - `шлюз = <ip-шлюза-vpn>`, если ваш VPN требует явный шлюз

2. Создайте другой outbound с именем `default` со следующими параметрами:
    - `тип = Таблица маршрутизации`
    - `ID таблицы = 254`

#### Почему `default`? {class="no-step-marker"}

В этом примере используется основная таблица маршрутизации Linux как запасной путь, поэтому `default` должен указывать на таблицу маршрутизации `254`.

### Добавьте DNS-серверы

Перейдите в **DNS-серверы** и создайте записи:

1. Создайте DNS-сервер с именем `vpn_dns` со следующими параметрами:
    - `адрес = <ip-dns-сервера-vpn>`
    - `outbound = vpn`, если вы хотите, чтобы DNS-запросы к этому серверу шли через VPN

2. Создайте другой DNS-сервер с именем `default_dns` со следующими параметрами:
    - `адрес = <ip-обычного-dns-сервера>`

### Создайте тестовый список

Перейдите в **Списки** и создайте список, например `my_sites` с типом **Домены / IP**, затем добавьте тестовый домен, например `ifconfig.co`.

### Добавьте правила маршрутизации и DNS

1. Перейдите в **Правила маршрутизации** и направьте `my_sites` через outbound `vpn`.
2. Перейдите в **DNS-правила** и отправьте `my_sites` на ваш DNS-сервер VPN.
3. Установите основной DNS-сервер на `default_dns`.

### Проверка

1. Примените конфигурацию и дождитесь исчезновения уведомления "черновик конфигурации".
2. Очистите DNS-кеш на вашем устройстве:
    - На Windows ПК откройте командную строку и выполните `ipconfig /flushdns`.
    - На Linux ПК откройте терминал и выполните `sudo resolvectl flush-caches`.
    - На мобильных устройствах просто переподключитесь к WiFi.
3. Откройте сайт из вашего списка (`ipconfig.co`) и проверьте, что IP отличается от другого сайта "мой IP" (напр. `wtfismyip.com`)

{{% /steps %}}

Для проверки настройки откройте сайт из вашего списка и убедитесь, что он работает через VPN. Если хотите проверить через командную строку, выполните:

```bash {filename="bash"}
keen-pbr test-routing ifconfig.co
```

Если результат показывает ваш VPN outbound в обоих столбцах (ожидаемый и фактический), настройка работает.

{{< /tab >}}
{{< tab name="JSON" >}}

Используйте этот путь, если вы установили `keen-pbr-headless` или предпочитаете редактировать файл конфигурации напрямую.

Расположение файла конфигурации:

- Keenetic / NetCraze: `/opt/etc/keen-pbr/config.json`
- OpenWrt: `/etc/keen-pbr/config.json`
- Debian: `/etc/keen-pbr/config.json`

Пример минимальной конфигурации:

```json {filename="config.json"}
{
  // Outbounds — это то, куда может идти ваш трафик
  "outbounds": [
    {
      "tag": "vpn",          // имя outbound; можно использовать буквы, цифры и подчёркивание
      "type": "interface",   // outbound типа "interface" может маршрутизировать трафик через конкретный интерфейс
      "interface": "tun0",
      "gateway": "10.8.0.1"
    },
    {
      "tag": "out",
      "type": "table",  // outbound типа "table" может маршрутизировать трафик в таблицу маршрутизации ядра iproute
      "table": 254      // ядру routing table Linux "main" имеет ID 254. См. файл /etc/iproute2/rt_tables для дополнительной информации.
    }
  ],
  "lists": {
    "my_sites": {   // список с доменами inline
      "domains": ["ifconfig.co"],
      "ttl_ms": 3600000 // как долго разрешённый IP добавляется в routing ipsets после разрешения dnsmasq, в миллисекундах
    },
    "always_out": { // список с IP inline
      "ip_cidrs": ["120.131.22.11"]
    },
    "my_remote_list": { // удалённый список
      "url": "https://example.com/my-list.lst",
      "ttl_ms": 0 // если ttl равен 0, то IP будет добавлен в ipset навсегда (直到 перезапуска keen-pbr)
    },
    "my_local_file_list": { // локальный файл списка
      "file": "/etc/keen-pbr/local.lst"
    }
  },
  "dns": {
    "system_resolver": {
      "type": "dnsmasq-nftset", // установите в "dnsmasq-ipset" для Keenetic/Netcraze или если используете iptables вместо nftables
      "address": "127.0.0.1"
    },
    "servers": [
      // DoH/DoT не поддерживается keen-pbr.
      // Установите dnscrypt-proxy2, AdGuardHome или другие резолверы для DoH
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
      { // Все домены из списка "my_sites" будут разрешаться через DNS-сервер vpn_dns
        "list": ["my_sites"],
        "server": "vpn_dns"
      }
    ],
    "fallback": ["default_dns"] // Основные upstream DNS-серверы
  },
  "route": {
    "rules": [
      { // Все IP и домены из списка "my_sites" будут маршрутизироваться на outbound "vpn"
        "list": ["my_sites"],
        "outbound": "vpn"
      },
      { // Все IP и домены из списка "always_out" будут маршрутизироваться на outbound "out"
        "list": ["always_out"],
        "outbound": "out"
      }
    ]
  }
}
```

{{< callout type="info" >}}
В этом примере outbound `out` повторно использует таблицу маршрутизации Linux `main` (с id `254`).
Если вы маршрутизируете трафик на эту таблицу, он будет следовать вашим системным маршрутам по умолчанию.
{{< /callout >}}

Что делает этот пример:

- Создаёт VPN outbound и outbound `out`, который повторно использует таблицу маршрутизации `254` (основная таблица маршрутизации)
- Добавляет список `my_sites` с `ifconfig.co`
- Добавляет список `always_out` с `120.131.22.11`
- Добавляет списки `my_remote_list` и `my_local_file_list`, которые не используются ни в каких правилах, но приведены здесь просто для примера
- Отправляет DNS-запросы для `my_sites` через `vpn_dns`
- Отправляет трафик для всех доменов из `my_sites` через `vpn`
- Отправляет трафик для всех IP из `always_out` через `out`

Перезапустите сервис после сохранения конфигурации:

{{< tabs >}}
{{< tab name="Keenetic / NetCraze" selected=true >}}
```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr restart
```
{{< /tab >}}
{{< tab name="OpenWrt" >}}
```bash {filename="bash"}
service keen-pbr restart
```
{{< /tab >}}
{{< tab name="Debian" >}}
```bash {filename="bash"}
systemctl restart keen-pbr
```
{{< /tab >}}
{{< /tabs >}}

Проверьте результат:

```bash {filename="bash"}
keen-pbr test-routing ifconfig.co
```

Вы также можете выполнить `keen-pbr status` для более широкой проверки состояния.

{{< /tab >}}
{{< /tabs >}}

{{< callout type="info" >}}
Для полного справочника начните с раздела [Конфигурация](../../configuration/). Если что-то не работает, см. раздел [Устранение неполадок](../../troubleshooting/).
{{< /callout >}}

