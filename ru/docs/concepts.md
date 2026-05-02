# Принцип работы

Для обычной настройки эту страницу читать необязательно.

Коротко: вы создаёте список сайтов, выбираете, какое соединение должно их обслуживать, а keen-pbr сам следит за тем, чтобы DNS и маршрутизация оставались согласованы и нужный трафик шёл нужным путём.

Эта страница объясняет, что происходит «под капотом», — для тех, кто хочет разобраться глубже.

## Основные сущности

### Списки

Именованные наборы IP-адресов, CIDR-диапазонов и доменных имён. Источники можно комбинировать в любых сочетаниях:
- **Удалённый URL** (`url`) — загружается и кэшируется при запуске, обновляется по расписанию
- **IP/CIDR напрямую** (`ip_cidrs`) — берутся прямо из конфига
- **Домены напрямую** (`domains`) — берутся прямо из конфига
- **Локальный файл** (`file`) — читается с диска

При запуске IP/CIDR-записи загружаются в ipsets или nftsets (`kpbr4_<list>`, `kpbr6_<list>`) без ограничения времени жизни записей.

Домены превращаются в директивы dnsmasq `ipset=`/`nftset=`: при разрешении домена его IP динамически добавляются в соответствующий набор (`kpbr4d_<list>`, `kpbr6d_<list>`) и хранятся там до истечения `ttl_ms`, заданного для этого списка.

См. [Списки](../configuration/lists/) для полного справочника.

### Outbounds

Именованные цели для исходящего трафика. Доступны пять типов:

| Тип | Описание |
|---|---|
| `interface` | Маршрутизация через конкретный сетевой интерфейс с необязательными IPv4/IPv6-шлюзами |
| `table` | Передать трафик в существующую таблицу маршрутизации ядра Linux |
| `blackhole` | Отбросить совпавший трафик |
| `ignore` | Пропустить без изменений (пакет идёт по системной маршрутизации) |
| `urltest` | Адаптивный выбор: проверяет кандидаты по задержке и выбирает наилучший в рамках заданного допуска; включает circuit breaker для предотвращения флаппинга |

Outbounds типа `interface` и `table` получают fwmark и запись в таблице маршрутизации. `urltest` выбирает из дочерних outbounds. `blackhole` создаёт правило сброса пакетов в firewall, `ignore` — правило пропуска.

Когда правило указывает на `ignore`, keen-pbr выставляет вердикт «пропустить» в firewall — дальнейшая обработка правил keen-pbr прекращается, а пакет остаётся без метки. Ни таблица маршрутизации, ни запись `ip rule` для такого совпадения не создаются, поэтому пакет продолжает путь через обычную системную маршрутизацию. Поскольку правила обрабатываются по принципу «первое совпадение побеждает», `ignore` чаще всего применяется для исключений, которые ставятся перед более широкими перехватывающими правилами.

См. [Outbounds](../configuration/outbounds/) для полного справочника.

### Правила маршрутизации

Упорядоченный список пар «условие → действие». Каждое правило может фильтровать трафик по:
- **Принадлежности к списку** — IP входит в именованный ipset/nftset
- **Протоколу** (`proto`) — `tcp`, `udp`
- **Портам** (`src_port`, `dest_port`) — одиночный, список, диапазон или отрицание
- **Адресам** (`src_addr`, `dest_addr`) — CIDR, список или отрицание

Если в правиле указано несколько условий, пакет должен удовлетворять им ВСЕМ.

Срабатывает первое подходящее правило. Трафик, не попавший ни под одно правило, остаётся без метки и идёт через обычную системную маршрутизацию.

См. [Правила маршрутизации](../configuration/route-rules/) для полного справочника.

### DNS

Связывает списки доменов с DNS-серверами через директивы dnsmasq `server=`. Когда запрашивается домен из списка, dnsmasq переадресует запрос назначенному серверу. Полученные IP-адреса одновременно добавляются в соответствующий ipset/nftset, чтобы последующие пакеты шли через правильный outbound.

Интеграция выполняется через `conf-file=` (или `conf-script=`): keen-pbr при запуске записывает файл `/tmp/keen-pbr-dnsmasq.conf`, который dnsmasq читает при следующей перезагрузке.

См. [DNS](../configuration/dns/) для полного справочника.

---

## Как это работает — последовательность запуска

1. **Загрузка списков** — скачиваются удалённые URL (при недоступности берётся кэш), читаются локальные файлы и встроенные записи
2. **Заполнение ipsets/nftsets** — IP/CIDR-записи из списков загружаются в ядерные наборы (`kpbr4_<list>`, `kpbr6_<list>`)
3. **Настройка правил firewall** — в таблице `mangle` iptables или в таблице `inet KeenPbrTable` nftables создаются правила сопоставления по спискам и фильтрам, после чего в `PREROUTING` / `prerouting` проставляются нужные fwmark
4. **Настройка маршрутизации** — под каждый outbound создаются таблица маршрутизации и запись `ip rule` на основе назначенных fwmark
5. **Генерация конфигурации резолвера** — файл `/tmp/keen-pbr-dnsmasq.conf` с директивами `server=` и `ipset=`/`nftset=` записывается на диск; dnsmasq получает сигнал перезагрузки
6. **Запуск urltest-проверок** — если настроены outbounds типа `urltest`, начинаются периодические замеры задержки

---

## Обзор архитектуры

```mermaid
flowchart TD
    subgraph Config["config.json"]
        RoutingOutbounds["Routing outbounds\n(interface, table,\nurltest-selected child)"]
        Lists["Lists\n(IPs, CIDRs, domains)"]
        DNS["DNS\n(servers + rules)"]
        RouteRules["Route Rules\n(list + filters →\nrouting / drop / pass)"]
    end

    subgraph Kernel["Linux Kernel"]
        Ipsets["ipsets / nftsets\n(kpbr4_&lt;list&gt;, kpbr6_&lt;list&gt;)"]
        FwmarkRules["Firewall rules\n(PREROUTING →\nmark, drop, or pass)"]
        IpRules["ip rules\n(fwmark → table)"]
        RoutingTables["Routing tables\n(table → interfaces)"]
        SystemRouting["System routing\n(default path)"]
    end

    Dnsmasq["dnsmasq\n(ipset= / nftset=)"]

    Lists -->|"IP/CIDR entries"| Ipsets
    Lists -->|"domain entries"| Dnsmasq
    DNS --> Dnsmasq
    Ipsets --> FwmarkRules
    RouteRules --> FwmarkRules
    RoutingOutbounds --> IpRules
    RoutingOutbounds --> RoutingTables
    FwmarkRules --> IpRules
    FwmarkRules --> SystemRouting
    IpRules --> RoutingTables
    Dnsmasq -->|"resolved IPs → ipset"| Ipsets
```

---

## Поток пакета во время выполнения

```mermaid
flowchart TD
    Packet(["Входящий пакет\n(напр. dest: 93.184.216.34)"])
    PREROUTING["Firewall PREROUTING\n(netfilter mangle)"]
    IpsetCheck{"IP в\nipset/nftset?"}
    NoMatch["Нет совпадения →\nнет fwmark\nсистемная маршрутизация"]
    Fwmark["Установить fwmark\n(напр. 0x00010000)"]
    IpRule["ip rule lookup\n(fwmark → table 150)"]
    RoutingTable["Таблица маршрутизации 150\ndefault via tun0 10.8.0.1"]
    Egress(["Пакет выходит через VPN\n(tun0)"])

    Packet --> PREROUTING
    PREROUTING --> IpsetCheck
    IpsetCheck -->|"нет"| NoMatch
    IpsetCheck -->|"да (list: my_domains)"| Fwmark
    Fwmark --> IpRule
    IpRule --> RoutingTable
    RoutingTable --> Egress
```

---

## Поток разрешения DNS

```mermaid
sequenceDiagram
    participant Client
    participant dnsmasq
    participant VPN_DNS as VPN DNS (10.8.0.1)
    participant Ipset as ipset kpbr4_my_domains
    participant Firewall

    Client->>dnsmasq: запрос example.com
    dnsmasq->>VPN_DNS: перенаправление (server=/example.com/10.8.0.1)
    VPN_DNS-->>dnsmasq: 93.184.216.34
    dnsmasq->>Ipset: добавить 93.184.216.34 (директива nftset=)
    dnsmasq-->>Client: 93.184.216.34

    Note over Client,Firewall: Следующий пакет на 93.184.216.34
    Client->>Firewall: пакет dest 93.184.216.34
    Firewall->>Firewall: совпадение ipset kpbr4_my_domains → установить fwmark
```

