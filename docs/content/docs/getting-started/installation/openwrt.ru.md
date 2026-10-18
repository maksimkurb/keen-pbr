---
title: OpenWrt
weight: 2
---

keen-pbr ставится на OpenWrt из репозитория пакетов keen-pbr. Команды выполняются по SSH.

{{% steps %}}

### Узнайте, какой менеджер пакетов у вашей OpenWrt

Менеджер пакетов — программа, которая устанавливает и обновляет другие программы. Какой из них у вас, зависит от версии:

- OpenWrt 25.x и новее: `apk`
- OpenWrt 24.x и старше: `opkg`

### Установите keen-pbr

1. Подключитесь к роутеру по SSH.
2. Откройте [страницу репозитория keen-pbr](https://repo.keen-pbr.fyi/repository/stable/?lang=ru), слева выберите ОС **OpenWrt** и свою версию и архитектуру. Выполните показанные команды по порядку.
3. Установите пакет:

```bash {filename="bash"}
# OpenWrt 25.x и новее
apk update
apk add keen-pbr

# версия без веб-интерфейса и API:
# apk add keen-pbr-headless
```

```bash {filename="bash"}
# OpenWrt 24.x и старше
opkg update
opkg install keen-pbr

# версия без веб-интерфейса и API:
# opkg install keen-pbr-headless
```

Конфигурация лежит в `/etc/keen-pbr/config.json`. Сервис запускается сам при каждой загрузке роутера.

{{< callout type="info" >}}
`keen-pbr-headless` занимает меньше места: около 1,2 МБ вместо 2,8 МБ. Веб-интерфейса и API в нём нет, настраивать придётся через файл конфигурации. Если сомневаетесь, ставьте обычный `keen-pbr`.
{{< /callout >}}

{{< callout type="warning" >}}
Пакет сам ставит нужные модули ядра. Если в логе есть строка `cannot bind netfilter queue/log ... Invalid argument`, установите их вручную: `kmod-nfnetlink-queue`, `kmod-nfnetlink-log` и `kmod-nft-queue`.
{{< /callout >}}

### Проверьте, что сервис запущен

```bash {filename="bash"}
service keen-pbr status
```

Если всё в порядке, команда напишет `running`.

### Откройте веб-интерфейс

Откройте в браузере `http://<ip-роутера>:12121`, например `http://192.168.1.1:12121`. В headless-версии веб-интерфейса нет.

### Настройте первые правила

Перейдите к [Быстрому старту]({{< relref "/docs/getting-started/quick-start" >}}) и откройте вкладку **Веб-интерфейс**. Если поставили `keen-pbr-headless`, откройте вкладку **JSON**.

{{< callout type="info" >}}
Если готовых пакетов для вашей платформы нет, соберите keen-pbr сами: [Сборка из исходного кода]({{< relref "/docs/developer/build-from-source" >}}).
{{< /callout >}}

{{% /steps %}}

## Команды для управления сервисом

```bash {filename="bash"}
service keen-pbr start
service keen-pbr restart
service keen-pbr status
service keen-pbr enable
```

`enable` включает автозапуск при загрузке роутера.

## Отдельные DNS-серверы для списков (dnsmasq) {#dnsmasq}

В OpenWrt dnsmasq уже работает как DNS-сервер роутера. Для маршрутизации keen-pbr он не нужен. Стоит ли отдать его под управление keen-pbr, читайте в разделе [Нужен ли dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Включение {#dnsmasq-install}

В веб-интерфейсе откройте **Общее** > **Настройки** > **Управление локальным DNS-сервером** и включите **Управлять конфигурацией dnsmasq**.

Пока управление включено и заданы **Основные DNS-серверы**, keen-pbr убирает из списка в `/etc/config/dhcp` (в LuCI **DNS forwardings**) серверы без домена. Серверы для отдельных доменов, например `/lan/192.168.1.1`, остаются. Убранные серверы вернутся, когда вы выключите управление или очистите эти серверы.

### Вернуть как было {#dnsmasq-rollback}

Выключите **Управлять конфигурацией dnsmasq** и примените конфигурацию. keen-pbr перезапустит dnsmasq и вернёт убранные серверы.
