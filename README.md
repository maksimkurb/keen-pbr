# Keenetic PBR

![workflow status](https://img.shields.io/github/actions/workflow/status/maksimkurb/keenetic-pbr/.github%2Fworkflows%2Fbuild-ci.yml?branch=main)
![release](https://img.shields.io/github/v/release/maksimkurb/keenetic-pbr?sort=date)

#### [> README in English <](./README.en.md)

Keenetic PBR — это пакет для маршрутизации на основе правил для роутеров Keenetic.

Telegram-чат проекта: https://t.me/keenetic_pbr

С помощью этого пакета можно настроить выборочную маршрутизацию для указанных IP-адресов, подсетей и доменов. Это необходимо, если вам понадобилось организовать защищенный доступ к определенным ресурсам, либо выборочно разделить трафик на несколько провайдеров (напр. трафик до сайта А идёт через одного оператора, а остальной трафик - через другого)

Пакет использует `ipset` для того, чтобы хранить большое количество адресов в памяти роутера без существенного увеличения нагрузки, а также `dnsmasq` для того, чтобы пополнять данный `ipset` IP-адресами, которые резолвят клиенты локальной сети.

Для настройки маршрутизации пакет создает скрипты в директории `/opt/etc/ndm/netfilter.d` и `/opt/etc/ndm/ifstatechanged.d`.

## Особенности

- Маршрутизация на основе доменов через `dnsmasq`
- Маршрутизация на основе IP-адресов через `ipset`
- Настраиваемые таблицы маршрутизации и приоритеты
- Автоматическая настройка для списков `dnsmasq`

## Установка (однострочная команда)

Подключитесь к вашему EntWare через SSH и выполните следующую команду:

```bash
opkg install curl jq && curl -sOfL https://raw.githubusercontent.com/maksimkurb/keenetic-pbr/refs/heads/main/install.sh && sh install
```

## Установка (ручной метод)

1. Перейдите на страницу [релизов](https://github.com/maksimkurb/keenetic-pbr/releases) и скопируйте URL для последнего `.ipk` файла
   для вашей архитектуры

2. Скачайте `.ipk` файл на ваш маршрутизатор:
   ```bash
   curl -LO <URL-to-latest-ipk-file-for-your-architecture>
   ```

3. **Установите его с помощью OPKG:**

   ```bash
   opkg install keenetic-pbr-*-entware.ipk
   ```

Это установит Keenetic PBR и настроит его на вашем маршрутизаторе.

## Настройка

Установщик заменяет оригинальный файл конфигурации **dnsmasq**.
Резервная копия вашего оригинального файла сохраняется как `/opt/etc/dnsmasq.conf.orig`.

Отредактируйте конфигурацию в следующих файлах в соответствии с вашими потребностями:

- **Конфигурация Keenetic-PBR:** `/opt/etc/keenetic-pbr/keenetic-pbr.conf`
- **Конфигурация dnsmasq:** `/opt/etc/dnsmasq.conf`

### 1. Редактирование `keenetic-pbr.conf`

Откройте `/opt/etc/keenetic-pbr/keenetic-pbr.conf` и отредактируйте его по мере необходимости.

Основное, что вы, вероятно, захотите изменить — это `interface` для маршрутизации.

```ini
[general]
# Путь к бинарному файлу `ipset`
ipset_path = "ipset"

# Выходной каталог для маршрутизационных списков
lists_output_dir = "/opt/etc/keenetic-pbr/lists.d"

# Загруженные списки будут сохранены в этом каталоге для dnsmasq
dnsmasq_lists_dir = "/opt/etc/dnsmasq.d"

# Если true, keenetic-pbr будет суммировать IP-адреса и CIDR перед применением к ipset
summarize = true

[[ipset]]
   # Имя ipset
   ipset_name = "vpn"
   
   # Очищать ipset каждый раз перед его загрузкой
   flush_before_applying = true

   [ipset.routing]
   # Целевой интерфейс для маршрутизации
   interface = "nwg1"
   
   # Этот fwmark будет применяться ко всем пакетам с IP-адресами из этого ipset
   fwmark = 1001
   
   # Номер таблицы маршрутизации для ip route
   table = 1001
   
   # Приоритет для ip rule
   priority = 1001

   # Списки для импорта в ipset/dnsmasq
   [[ipset.list]]
   # Имя списка 1
   name = "local"
   # Список хостов списка 1
   hosts = ["ifconfig.co", "myip2.ru", "1.2.3.4", "141.201.11.0/24"]
   
   [[ipset.list]]
   # Имя списка 2
   name = "list-name"
   # URL списка 2 (файл должен содержать домены, IP адреса и CIDR, по одному на каждой строчке)
   url = "https://some-url/list1.lst"
   
   [[ipset.list]]
   # Имя списка 3
   name = "re-filter-ipsum"
   # URL списка 3 (файл должен содержать домены, IP адреса и CIDR, по одному на каждой строчке)
   url = "https://some-url/list2.lst"

# Вы можете добавлять столько ipset, сколько хотите:

# [[ipset]]
#   ipset_name = "direct"
#   flush_before_applying = true
#
#   [ipset.routing]
#   interface = "ppp0"
#   fwmark = 998
#   table = 998
#   priority = 998
#   
#   [[ipset.list]]
#   name = "list-name"
#   url = "https://some-url/list1.lst"
#
#   [[ipset.list]]
#   name = "re-filter-ipsum"
#   url = "https://some-url/list2.lst"
#
# [[ipset]]
# ...
#
#   [ipset.routing]
#   ...
#  
#   [[ipset.list]]
#   ...
```

### 2. Скачивание списков

После редактирования конфигурационного файла скачайте файлы списков

```bash
keenetic-pbr download
```

### 3. Включение DNS-override

1. Откройте следующий URL в браузере:
   ```
   http://<router-ip-address>/a
   ```
2. Введите следующие команды:
   1. `opkg dns-override`
   2. `system configuration save`

### 4. Перезапуск OPKG и проверка работы маршрутизации

Перезапустите OPKG и убедитесь, что маршрутизация на основе политики работает должным образом.

## Обновление списков
Списки обновляются ежедневно автоматически с помощью cron.

Вы можете обновить списки вручную, выполнив следующие команды:

```bash
keenetic-pbr download
/opt/etc/init.d/S80keenetic-pbr restart
```

## Устранение неполадок

Если возникают проблемы, проверьте ваши конфигурационные файлы и логи.
Убедитесь, что списки были загружены правильно, и что `dnsmasq` работает с обновленной конфигурацией.

С вопросами можно обращаться в Telegram-чат проекта: https://t.me/keenetic_pbr

---

Приятного использования маршрутизации на основе политики с Keenetic-PBR!
