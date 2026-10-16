# Debian

keen-pbr можно установить на системы Debian из APT-репозитория keen-pbr.

{{% steps %}}

### Добавьте репозиторий со страницы репозитория

Откройте страницу инструкций репозитория, выберите **Debian** в селекторе ОС слева и используйте сгенерированные команды для вашего выпуска и архитектуры:

[Репозиторий keen-pbr](https://repo.keen-pbr.fyi/repository/stable/?lang=ru)

Сгенерированные инструкции включают ключ подписи и запись источника `apt`.

### Установите пакет

Пример команды установки:

```bash {filename="bash"}
apt update
apt install keen-pbr

# или если нужна версия без API и без веб-интерфейса
# apt update
# apt install keen-pbr-headless
```

Пакет устанавливает конфигурацию в `/etc/keen-pbr/config.json` и автоматически включает systemd-сервис.

Полезные команды сервиса:

```bash {filename="bash"}
systemctl start keen-pbr
systemctl status keen-pbr
systemctl restart keen-pbr
```

{{< callout type="info" >}}
Если вы не планируете использовать веб-интерфейс keen-pbr или API, пакет `keen-pbr-headless` занимает меньше места (~1.2 МБ вместо ~2.8 МБ) и не включает API-сервер. Также вы можете отключить API-сервер через флаг конфигурации в любой момент в полной версии пакета.
{{< /callout >}}

### Следующие шаги

Откройте [Быстрый старт]({{< relref "/docs/getting-started/quick-start" >}}) и используйте вкладку **Веб-интерфейс** для самой простой первоначальной настройки. Если вы установили `keen-pbr-headless`, используйте вкладку **JSON / CLI**.

{{< callout type="info" >}}
Если готовые пакеты ещё не доступны для вашей платформы, см. раздел [Сборка из исходного кода]({{< relref "/docs/developer/build-from-source" >}}), чтобы собрать keen-pbr самостоятельно.
{{< /callout >}}

{{% /steps %}}

## Отдельные DNS-серверы для списков (dnsmasq) {#dnsmasq}

Для маршрутизации keen-pbr не нужен dnsmasq. Ставьте его, если хотите отдельные DNS-серверы для некоторых списков или свои DNS-серверы по умолчанию. Подробнее: [Нужен ли dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Установка dnsmasq {#dnsmasq-install}

```bash {filename="bash"}
apt install dnsmasq
```

Чтобы применить рекомендуемые настройки dnsmasq, выполните:

```bash {filename="bash"}
/usr/lib/keen-pbr/dnsmasq-hook.sh install-recommended-config
```

Команда заменит `/etc/dnsmasq.conf`, прежний файл сохранится как `/etc/dnsmasq.conf.backup-keenpbr`. После этого dnsmasq отвечает только вашим локальным сетям и берёт серверы у keen-pbr.

Затем в веб-интерфейсе откройте **Общее** > **Управление локальным DNS-сервером** и включите **Управлять конфигурацией dnsmasq**. keen-pbr добавит в `/etc/dnsmasq.conf` помеченный блок; в рекомендуемом конфиге он уже есть. Если система ещё не использует dnsmasq, направьте на него системный резолвер или DHCP-сервер.

### Вернуть как было {#dnsmasq-rollback}

1. Выключите **Управлять конфигурацией dnsmasq** и примените конфигурацию. dnsmasq перезапустится и будет использовать серверы из `/etc/keen-pbr/dnsmasq-fallback.conf`. Если у dnsmasq есть свои серверы, эти добавятся к ним. Чтобы этого не было, очистите файл.
2. Верните системный резолвер, который был до dnsmasq.
3. Если запускали `install-recommended-config`, верните прежние настройки:
   ```bash {filename="bash"}
   cp /etc/dnsmasq.conf.backup-keenpbr /etc/dnsmasq.conf
   systemctl restart dnsmasq
   ```
4. Или удалите пакет:
   ```bash {filename="bash"}
   apt remove dnsmasq
   ```

