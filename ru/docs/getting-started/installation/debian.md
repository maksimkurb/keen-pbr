# Debian

keen-pbr ставится на Debian из APT-репозитория keen-pbr. Команды выполняются по SSH или в терминале сервера от имени `root` (или через `sudo`).

{{% steps %}}

### Добавьте репозиторий

Откройте [страницу репозитория keen-pbr](https://repo.keen-pbr.fyi/repository/stable/?lang=ru), слева выберите ОС **Debian**, свой выпуск и архитектуру. Выполните показанные команды по порядку: они добавляют ключ подписи и источник пакетов `apt`.

### Установите пакет

```bash {filename="bash"}
apt update
apt install keen-pbr

# версия без веб-интерфейса и API:
# apt update
# apt install keen-pbr-headless
```

Конфигурация лежит в `/etc/keen-pbr/config.json`. Сервис запускается сразу и стартует при каждой загрузке системы.

{{< callout type="info" >}}
`keen-pbr-headless` занимает меньше места: около 1,2 МБ вместо 2,8 МБ. Веб-интерфейса и API в нём нет, настраивать придётся через файл конфигурации. Если сомневаетесь, ставьте обычный `keen-pbr`.
{{< /callout >}}

### Проверьте, что сервис запущен

```bash {filename="bash"}
systemctl status keen-pbr
```

Если всё в порядке, в выводе будет `active (running)`.

### Откройте веб-интерфейс

Откройте в браузере `http://<ip-сервера>:12121`. В headless-версии веб-интерфейса нет.

### Настройте первые правила

Перейдите к [Быстрому старту]({{< relref "/docs/getting-started/quick-start" >}}) и откройте вкладку **Веб-интерфейс**. Если поставили `keen-pbr-headless`, откройте вкладку **JSON**.

{{< callout type="info" >}}
Если готовых пакетов для вашей платформы нет, соберите keen-pbr сами: [Сборка из исходного кода]({{< relref "/docs/developer/build-from-source" >}}).
{{< /callout >}}

{{% /steps %}}

## Команды для управления сервисом

```bash {filename="bash"}
systemctl start keen-pbr
systemctl status keen-pbr
systemctl restart keen-pbr
```

## Отдельные DNS-серверы для списков (dnsmasq) {#dnsmasq}

Для маршрутизации dnsmasq не нужен. dnsmasq — DNS-сервер, который ставится на систему рядом с keen-pbr. Ставьте его, если хотите отправлять домены некоторых списков на отдельные DNS-серверы или задать свои основные DNS-серверы. Подробнее: [Нужен ли dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Установка dnsmasq {#dnsmasq-install}

```bash {filename="bash"}
apt install dnsmasq
```

Чтобы применить рекомендуемые настройки dnsmasq, выполните:

```bash {filename="bash"}
/usr/lib/keen-pbr/dnsmasq-hook.sh install-recommended-config
```

Команда заменит `/etc/dnsmasq.conf`, прежний файл сохранится как `/etc/dnsmasq.conf.backup-keenpbr`. После этого dnsmasq отвечает только вашим локальным сетям и берёт серверы у keen-pbr.

Затем в веб-интерфейсе откройте **Общее** > **Настройки** > **Управление локальным DNS-сервером** и включите **Управлять конфигурацией dnsmasq**. Если система ещё не использует dnsmasq, направьте на него системный резолвер или DHCP-сервер.

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

