# OpenWrt

keen-pbr можно установить на роутерах OpenWrt из репозитория пакетов keen-pbr.

## 1. Проверьте, какой менеджер пакетов использует ваша версия OpenWrt

Страница репозитория автоматически показывает правильный путь для вашей целевой системы:

- OpenWrt 25.x и новее: `apk`
- OpenWrt 24.x и старше: `opkg`

## 2. Замените `dnsmasq` на `dnsmasq-full`

Установите `dnsmasq-full` перед установкой keen-pbr:

{{< callout type="info" >}}
Для OpenWrt 25.x и новее тоже нужен `dnsmasq-full` вместо стандартного `dnsmasq`.
У меня нет роутера с `apk`, чтобы проверить точные шаги замены. Если вы знаете правильную процедуру, пожалуйста, отправьте PR с исправлением документации.
{{< /callout >}}

```bash {filename="bash"}
# OpenWrt 25.x и новее
apk --update-cache add dnsmasq-full

# OpenWrt 24.x и старше
opkg update && cd /tmp/ && opkg download dnsmasq-full
opkg remove dnsmasq; opkg install dnsmasq-full --cache /tmp/; rm -f /tmp/dnsmasq-full*.ipk;
```

## 3. Установите из страницы репозитория

Откройте страницу инструкций репозитория, выберите **OpenWrt** в селекторе ОС слева и используйте сгенерированные команды для вашей точной версии и архитектуры:

{{< hextra/hero-button text="Репозиторий keen-pbr" link="https://repo.keen-pbr.fyi/repository/stable/?lang=ru" >}}

Примеры команд установки:

```bash {filename="bash"}
# OpenWrt 25.x и новее
apk update
apk add keen-pbr

# или если нужна версия без API и без веб-интерфейса
# apk update
# apk add keen-pbr-headless
```

```bash {filename="bash"}
# OpenWrt 24.x и старше
opkg update
opkg install keen-pbr

# или если нужна версия без API и без веб-интерфейса
# opkg update
# opkg install keen-pbr-headless
```

Пакет устанавливает конфигурацию в `/etc/keen-pbr/config.json` и автоматически включает init-скрипт.

Полезные команды сервиса:

```bash {filename="bash"}
service keen-pbr start
service keen-pbr enable
service keen-pbr restart
```

{{< callout type="info" >}}
Если вы не планируете использовать веб-интерфейс keen-pbr или API, можно установить пакет `keen-pbr-headless`.
Он занимает меньше места (~1.2 МБ вместо ~2.8 МБ) и не включает API-сервер. Также вы можете отключить API-сервер через флаг конфигурации в любой момент в полной версии пакета.
{{< /callout >}}

Следующий шаг: откройте [Быстрый старт](../quick-start/) и используйте вкладку **Веб-интерфейс** для самой простой первоначальной настройки. Если вы установили `keen-pbr-headless`, используйте вкладку **JSON / CLI**.

{{< callout type="info" >}}
Если готовые пакеты ещё не доступны для вашей платформы, см. раздел [Сборка из исходного кода](../compilation/), чтобы собрать keen-pbr самостоятельно.
{{< /callout >}}

