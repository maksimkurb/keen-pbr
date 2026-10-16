---
title: Обновление с 2.x
---

Это руководство для тех, у кого на Keenetic / NetCraze уже работает keen-pbr 2.x. Вы замените репозиторий и пакет и перенесёте конфигурацию в новый формат. Все команды выполняются по SSH.

## 1. Сделайте резервную копию

Сохраните текущую конфигурацию 2.x. Если что-то пойдёт не так, вы сможете сравнить её с результатом конвертации:

```bash {filename="bash"}
cp /opt/etc/keen-pbr/keen-pbr.conf /opt/etc/keen-pbr/keen-pbr.conf.bak
```

## 2. Замените репозиторий

Откройте страницу репозитория, слева выберите ОС **Keenetic / NetCraze** и архитектуру своего роутера. Выполните показанные команды по порядку, чтобы заменить старый репозиторий keen-pbr 2.x на текущий:

{{< hextra/hero-button text="Репозиторий keen-pbr" link="https://repo.keen-pbr.fyi/repository/stable/?lang=ru" >}}

## 3. Установите keen-pbr

```bash {filename="bash"}
opkg install keen-pbr

# версия без веб-интерфейса и API:
# opkg install keen-pbr-headless
```

Новая конфигурация лежит в `/opt/etc/keen-pbr/config.json`.

## 4. Сконвертируйте конфигурацию в новый формат

Откройте [конвертер конфига keen-pbr](https://keen-pbr.fyi/converter/?lang=ru). Затем:

1. Скопируйте содержимое `/opt/etc/keen-pbr/keen-pbr.conf` в поле ввода конвертера слева.
2. Проверьте сгенерированный конфиг.
3. Замените содержимое `/opt/etc/keen-pbr/config.json` на результат конвертации.

{{< callout type="info" >}}
Заменяйте конфигурационный файл только после проверки содержимого.
{{< /callout >}}

## 5. Перезапустите keen-pbr

```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr restart
/opt/etc/init.d/S80keen-pbr status
```

Если всё в порядке, `status` напишет `alive`. Если написано `dead`, откройте раздел [Сервис не запускается]({{< relref "/docs/troubleshooting#service-does-not-start" >}}).

Затем откройте [Быстрый старт]({{< relref "/docs/getting-started/quick-start" >}}): вкладка **Веб-интерфейс** поможет проверить настройку, вкладка **JSON** нужна, если вы работаете с файлом конфигурации.
