---
title: Сборка из исходного кода
weight: 1
aliases:
  - /docs/getting-started/compilation/
---

Эта страница для разработчиков и мейнтейнеров пакетов, которые хотят собрать keen-pbr из исходного кода. Если вам нужен готовый пакет, смотрите [установку]({{< relref "/docs/getting-started" >}}).

## Предварительные требования

| Требование | Версия |
|---|---|
| C++ компилятор | Поддержка C++17 (GCC 8.4+, Clang 9+) |
| CMake | 3.14+ |
| Make | любой |
| pkg-config | любой |
| git | любой |
| Docker с Buildx (для сборки пакетов) | любой |

## Зависимости

Часть библиотек входит в репозиторий как git-подмодули (папка `third_party`), остальные берутся из системных пакетов при сборке CMake. Для сборки на хосте установите заголовочные файлы системных библиотек (в Debian и Ubuntu это пакеты `libcurl4-openssl-dev`, `libnl-3-dev`, `libnl-route-3-dev`, `zlib1g-dev`).

| Библиотека | Откуда | Назначение |
|---|---|---|
| libcurl | система | Загрузка удалённых списков |
| libnl-3, libnl-route-3 | система | Работа с маршрутами и правилами через Netlink |
| zlib | система | Нужна встроенному HTTP-серверу (при `WITH_API=ON`) |
| libresolv | входит в libc | Разрешение DNS-имён |
| libunwind | система | Стек вызовов при сбоях. В сборках Debian нужна по умолчанию. Сборки OpenWrt и Keenetic обходятся без неё и показывают при сбое только регистры. Параметр `KEEN_PBR_USE_LIBUNWIND` управляет поиском. |
| nlohmann_json | подмодуль | Разбор JSON |
| fmt | подмодуль | Форматирование строк |
| Monocypher | подмодуль | Криптография (хеширование паролей) |
| cpp-httplib | подмодуль | Встроенный HTTP-сервер при `WITH_API=ON` |

## Сборка

`Makefile` оборачивает все шаги сборки:

```bash {filename="bash"}
# Клонировать репозиторий вместе с подмодулями
git clone --recurse-submodules https://github.com/maksimkurb/keen-pbr.git
cd keen-pbr

# Собрать
make

# Запустить тесты (нужны python3 и sh; dnsmasq необязателен)
make test

# Очистить артефакты сборки
make clean
```

Пошаговые цели также доступны:

```bash {filename="bash"}
make setup    # cmake -S . -B cmake-build-gcc
make build    # cmake --build cmake-build-gcc
```

Бинарный файл создаётся в `cmake-build-gcc/keen-pbr`. Цели Clang (`make clang-build`) собирают в `cmake-build-clang`.

Запустите `make help`, чтобы увидеть все цели сборки и упаковки.

## Опции сборки

Передавайте опции при настройке `cmake`:

| Опция | По умолчанию | Описание |
|---|---|---|
| `WITH_API` | `ON` | Собирать со встроенным HTTP-сервером (REST API и веб-интерфейс) |
| `USE_KEENETIC_API` | `OFF` | Включить интеграцию с Keenetic (DNS-сервер типа `keenetic`) |
| `KEEN_PBR_PLATFORM_KEENETIC` | `OFF` | Сборка для KeeneticOS: только iptables, без балансировки |
| `KEEN_PBR_USE_LIBUNWIND` | `ON` | Искать и использовать `libunwind` для диагностики сбоев. Сборки Debian требуют её при включённом параметре; OpenWrt и Keenetic при её отсутствии или выключенном параметре используют диагностику только по регистрам. |
| `BUILD_TESTS` | `OFF` | Собирать модульные тесты (`make test` включает его сам) |

Пример: сборка без HTTP-сервера (как пакет `keen-pbr-headless`):

```bash {filename="bash"}
cmake -S . -B cmake-build-gcc -DWITH_API=OFF
cmake --build cmake-build-gcc
```

## Пакеты Debian / Ubuntu

Пакеты для Debian и Ubuntu собираются в Docker целями корневого `Makefile`. Нужны Docker и плагин Buildx. Выполните из корня репозитория:

```bash {filename="bash"}
make deb-packages
```

Релиз Debian (`bookworm` или `trixie`) и архитектуру (`amd64` или `arm64`) можно указать явно. По умолчанию `bookworm` и `amd64`:

```bash {filename="bash"}
# AMD64
make deb-packages DEBIAN_VERSION=bookworm DEBIAN_ARCH=amd64

# ARM64 (Docker автоматически использует linux/arm64)
make deb-packages DEBIAN_VERSION=bookworm DEBIAN_ARCH=arm64
```

Образ сборщика Debian пересоздаётся командой:

```bash {filename="bash"}
make debian-builder-image
```

Готовые пакеты (полный и `headless`) появятся в `build/packages/`:

- `build/packages/debian/<debian-version>/<architecture>/keen-pbr_<version>_debian_<architecture>.deb`
- `build/packages/debian/<debian-version>/<architecture>/keen-pbr-headless_<version>_debian_<architecture>.deb`

Опубликованный репозиторий также содержит пакеты для Ubuntu: Ubuntu noble (24.04) использует пакеты Debian bookworm, а Ubuntu resolute (26.04) использует пакеты Debian trixie.

Для полного пакета интерфейс собирается с помощью `bun`. При установке пакета сервис `keen-pbr` (юнит `systemd`) включается и запускается, а конфигурация лежит в `/etc/keen-pbr`.

## Сборка пакетов для роутеров

Пакеты для роутеров тоже собираются в Docker целями корневого `Makefile`. Они повторяют сборку в GitHub Actions, готовые пакеты появляются в `build/packages/`.

### Entware (для роутеров Keenetic и Netcraze)

Соберите пакет {{< term "Entware" >}}:

```bash {filename="bash"}
make keenetic-packages KEENETIC_CONFIG=mipsel-3.4 KEENETIC_VERSION=current
```

Параметры:

| Переменная | Обязательно | Пример | Описание |
|---|---|---|---|
| `KEENETIC_VERSION` | да | `current` | Канал прошивки Keenetic: имя подпапки в `build/packages/keenetic/`. |
| `KEENETIC_CONFIG` | да | `mipsel-3.4` | Тег архитектуры сборщика Entware.<br>Поддерживаемые значения: <br>`aarch64-3.10`, <br>`mips-3.4`, <br>`mipsel-3.4`, <br>`armv7-3.2`, <br>`x64-3.2`. |

Поведение:

- Используется готовый образ `ghcr.io/maksimkurb/entware-builder:<config>`.
- Репозиторий монтируется в контейнер в `/workspace`.
- Пакеты (`keen-pbr` и `keen-pbr-headless`) копируются в `build/packages/keenetic/<версия>/<архитектура>/`, например `keen-pbr_<version>_keenetic_mipsel-3.4.ipk`.

### OpenWrt

Сначала перечислите доступные архитектуры:

```bash {filename="bash"}
make list-openwrt-architectures OPENWRT_VERSION=24.10.4
```

Соберите пакет OpenWrt:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_ARCHITECTURE=aarch64_cortex-a53
```

Другой пример:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=25.12.2 OPENWRT_ARCHITECTURE=x86_64
```

Параметры:

| Переменная | Обязательно | По умолчанию | Пример | Описание |
|---|---|---|---|---|
| `OPENWRT_VERSION` | да | - | `24.10.4` | Версия OpenWrt (по ней выбирается образ SDK). |
| `OPENWRT_ARCHITECTURE` | да | - | `aarch64_cortex-a53` | Архитектура пакетов OpenWrt. |

Поведение:

- Используется официальный образ SDK `ghcr.io/openwrt/sdk:<architecture>-<version>`.
- Репозиторий монтируется в контейнер в `/workspace`, поэтому правки в исходниках подхватываются при следующей сборке.
- Пакеты `.ipk` / `.apk` копируются в `build/packages/openwrt/<версия>/<архитектура>/`.

## Развёртывание на роутер

Скопируйте собранный пакет на роутер и установите его (замените `192.168.1.1` на адрес роутера):

```bash {filename="bash"}
# Пример для Keenetic
scp build/packages/keenetic/<keenetic_version>/<pkg_arch>/keen-pbr_<version>_keenetic_<config>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_keenetic_<config>.ipk

# Пример для OpenWrt .ipk
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.ipk

# Пример для OpenWrt .apk
# имя файла .apk сохраняется таким, каким его создал SDK OpenWrt
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr-<version>.apk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 apk add --allow-untrusted /tmp/keen-pbr-<version>.apk
```

После установки сервис обычно уже запущен. Если нет, запустите его на роутере:

```bash {filename="bash"}
# Keenetic
/opt/etc/init.d/S80keen-pbr start

# OpenWrt
service keen-pbr start
```
