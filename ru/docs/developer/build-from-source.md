# Сборка из исходного кода

Эта страница для разработчиков и мейнтейнеров пакетов, которые хотят собрать keen-pbr из исходного кода.

## Предварительные требования

| Требование | Версия |
|---|---|
| C++ компилятор | Поддержка C++17 (GCC 8.4+, Clang 9+) |
| CMake | 3.14+ |
| Make | любой |
| pkg-config | любой |
| Docker (для кросс-компиляции) | любой |

## Зависимости

Зависимости объединены как git-подмодули или разрешаются из системных пакетов во время сборки CMake.

| Библиотека | Назначение |
|---|---|
| libcurl | Загрузка удалённых списков |
| nlohmann_json | JSON-парсинг |
| libnl | Коммуникация через сокеты Netlink (маршрутизация/правила) |
| libunwind | Обязательная build-time зависимость для backend разворачивания стека в crash diagnostics; по умолчанию линкуется статически, если доступен `libunwind.a` |
| fmt | C++17 formatting polyfill |
| cpptrace | Вендорная библиотека crash diagnostics |
| cpp-httplib | Встроенный HTTP API сервер |

## Сборка

`Makefile` оборачивает все шаги сборки:

```bash {filename="bash"}
# Клонировать репозиторий
git clone https://github.com/maksimkurb/keen-pbr.git
cd keen-pbr

# Собрать
make

# Запустить тесты
make test

# Очистить артефакты сборки
make clean
```

Пошаговые цели также доступны:

```bash {filename="bash"}
make setup    # cmake -S . -B cmake-build
make build    # cmake --build cmake-build
```

Бинарный файл производится в `cmake-build/keen-pbr`.

Запустите `make help`, чтобы увидеть все доступные цели сборки и упаковки.

## Опции сборки

Передавайте опции во время конфигурации `cmake`:

| Опция | По умолчанию | Описание |
|---|---|---|
| `WITH_API` | `ON` | Собирать со встроенным HTTP API сервером |
| `KEEN_PBR_STATIC_LIBUNWIND` | `ON` | Предпочитать статическую линковку `libunwind`; установите `OFF`, чтобы принудительно использовать shared library |

Пример:

```bash {filename="bash"}
cmake -S . -B cmake-build -DWITH_API=OFF
```

## Пакеты Debian / Ubuntu

Для целей Debian/Ubuntu используйте поддерживаемые Docker-ом цели сборки пакетов из корневого `Makefile`.

Собирайте пакеты Debian из корня репозитория:

```bash {filename="bash"}
make deb-packages
```

Используйте явную метку выпуска Debian внутри `build/packages/`:

```bash {filename="bash"}
make deb-packages DEBIAN_VERSION=bookworm
```

Пересобирайте образ Debian builder явным образом:

```bash {filename="bash"}
make debian-builder-image
```

Артефакты записываются в `build/packages/`:

- `build/packages/keen-pbr_<version>_debian_amd64.deb`
- `build/packages/keen-pbr-headless_<version>_debian_amd64.deb`

Поток упаковки Debian:

- собирает фронтенд с `bun` для полного пакета
- компилирует нативные бинарники с путями установки Debian под `/etc/keen-pbr` и `/usr/share/keen-pbr/frontend`
- устанавливает `systemd` юнит в `/lib/systemd/system/keen-pbr.service`
- не автовключает и не автозапускает сервис во время установки пакета

## Сборка пакетов для роутеров

Для целей роутеров используйте поддерживаемые Docker-ом цели сборки пакетов из корневого `Makefile`. Они отражают рабочие процессы GitHub Actions и записывают нормализованные артефакты в `build/packages/`.

### Entware (для роутеров Keenetic и Netcraze)

Соберите пакет Entware:

```bash {filename="bash"}
make keenetic-packages KEENETIC_CONFIG=mipsel-3.4 KEENETIC_VERSION=current
```

Параметры:

| Переменная | Обязательно | Пример | Описание |
|---|---|---|---|
| `KEENETIC_VERSION` | да | `current` | Версия канала Keenetic, используемая в макете `build/packages`. |
| `KEENETIC_CONFIG` | да | `mipsel-3.4` | Тег архитектуры сборщика Entware.<br>Поддерживаемые значения: <br>`aarch64-3.10`, <br>`mips-3.4`, <br>`mipsel-3.4`, <br>`armv7-3.2`, <br>`x64-3.2`. |

Поведение:

- Использует предварительно собранный образ `ghcr.io/maksimkurb/entware-builder:<config>`
- Монтирует репозиторий в контейнер в `/workspace`
- Собирает и собирает артефакты в `build/packages/`

### OpenWrt

Сначала перечислите доступные архитектуры:

```bash {filename="bash"}
make list-openwrt-architectures OPENWRT_VERSION=24.10.4
```

Соберите пакет OpenWrt:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_ARCHITECTURE=aarch64_cortex-a53
```

Или с явным выпуском:

```bash {filename="bash"}
make openwrt-packages OPENWRT_VERSION=24.10.4 OPENWRT_ARCHITECTURE=aarch64_cortex-a53

# ещё один распространённый пример
make openwrt-packages OPENWRT_VERSION=25.12.2 OPENWRT_ARCHITECTURE=x86_64
```

Параметры:

| Переменная | Обязательно | По умолчанию | Пример | Описание |
|---|---|---|---|---|
| `OPENWRT_VERSION` | да | - | `24.10.4` | Выпуск OpenWrt, используемый для выбора образа SDK. |
| `OPENWRT_ARCHITECTURE` | да | - | `aarch64_cortex-a53` | Архитектура пакетов OpenWrt и префикс тега образа SDK. |

Поведение:

- Монтирует репозиторий в контейнер в `/workspace`
- Использует официальный образ SDK `ghcr.io/openwrt/sdk:<architecture>-<version>`
- Копирует полученные артефакты `.ipk` / `.apk` в `build/packages/`

### Примечания

- Репозиторий монтируется в `/workspace`, поэтому изменения исходного кода и скриптов упаковки подхватываются при следующей сборке.
- Пакеты OpenWrt собираются внутри образа SDK, соответствующего выбранным версии и архитектуре.

## Развёртывание на роутер

Скопируйте собранный пакет и файл конфигурации на ваш роутер:

```bash {filename="bash"}
# Пример для Keenetic
scp build/packages/keenetic/<keenetic_version>/<pkg_arch>/keen-pbr_<version>_keenetic_<config>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_keenetic_<config>.ipk

# Пример для OpenWrt .ipk
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.ipk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 opkg install /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.ipk

# Пример для OpenWrt .apk
scp build/packages/openwrt/<openwrt_version>/<pkg_arch>/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.apk root@192.168.1.1:/tmp/
ssh root@192.168.1.1 apk add --allow-untrusted /tmp/keen-pbr_<version>_openwrt_<openwrt_version>_<architecture>.apk

```

Запустите на роутере:

```bash {filename="bash"}
# Keenetic
/opt/etc/init.d/S80keen-pbr start

# OpenWrt
service keen-pbr start
```

