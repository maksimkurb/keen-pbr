---
title: OpenWrt
weight: 2
---

keen-pbr можно установить на роутерах OpenWrt из репозитория пакетов keen-pbr.

{{% steps %}}

### Проверьте, какой менеджер пакетов использует ваша версия OpenWrt

Страница репозитория автоматически показывает правильный путь для вашей целевой системы:

- OpenWrt 25.x и новее: `apk`
- OpenWrt 24.x и старше: `opkg`

### Установите со страницы репозитория

Откройте страницу инструкций репозитория, выберите **OpenWrt** в селекторе ОС слева и используйте сгенерированные команды для вашей точной версии и архитектуры:

[Репозиторий keen-pbr](https://repo.keen-pbr.fyi/repository/stable/?lang=ru)

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

{{< callout type="warning" >}}
Для перехвата DNS и L7 нужны модули ядра `nfnetlink_queue`, `nfnetlink_log` и `nft_queue`.
Пакет автоматически подтягивает `kmod-nfnetlink-queue`, `kmod-nfnetlink-log` и `kmod-nft-queue`. Если вы ставили старую сборку или удалили их, установите вручную; иначе демон пишет `cannot bind netfilter queue/log ... Invalid argument`, а `/api/health/service` называет отсутствующий модуль. `nft_log` входит в `kmod-nft-core`.
{{< /callout >}}

### Следующие шаги

Откройте [Быстрый старт]({{< relref "/docs/getting-started/quick-start" >}}) и используйте вкладку **Веб-интерфейс** для самой простой первоначальной настройки. Если вы установили `keen-pbr-headless`, используйте вкладку **JSON / CLI**.

{{< callout type="info" >}}
Если готовые пакеты ещё не доступны для вашей платформы, см. раздел [Сборка из исходного кода]({{< relref "/docs/developer/build-from-source" >}}), чтобы собрать keen-pbr самостоятельно.
{{< /callout >}}

{{% /steps %}}

## Обновление с интеграции dnsmasq

Старые версии keen-pbr управляли dnsmasq: переносили его upstream-серверы в `dhcp.@dnsmasq[*].kpbr_server`, добавляли `conf-script` и монтирования jail. Интеграция удалена, а `dnsmasq-full` больше не нужен. При обновлении пакета keen-pbr сам отменяет эти изменения:

- upstream-серверы из `kpbr_server` возвращаются в `server` (без дубликатов, в исходном порядке), после чего `kpbr_server` удаляется;
- записи `addnmount` keen-pbr (`/usr/sbin/keen-pbr`, `/etc/keen-pbr`, `/var/cache/keen-pbr`, `/var/run/keen-pbr`) и drop-in `keen-pbr.conf` в `confdir` dnsmasq удаляются;
- UCI `dhcp` фиксируется (commit), dnsmasq перезапускается один раз. Повторный запуск ничего не меняет, а секции dnsmasq, которых keen-pbr не касался, остаются как есть.

Миграцию можно запустить вручную: `/usr/lib/keen-pbr/uci.sh dnsmasq-migrate-from-keen-pbr`.
