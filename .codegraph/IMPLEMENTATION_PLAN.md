# План реализации надёжного re-apply маршрутизации и DNS

## Цель и границы

Цель — исключить дополнительные утечки и разрывы, которые создаёт сам `keen-pbr` при повторном применении состояния, сделать активный daemon единственным источником правды для operational-команд и ввести проверяемую транзакционную модель применения конфигурации.

План не обещает устранить внешнее разрушение таблиц самим Keenetic до получения `SIGUSR1`. Гарантия для `SIGUSR1` формулируется так: если объекты ядра сохранились, `keen-pbr` не создаёт дополнительного disruption; если Keenetic уже удалил их, daemon восстанавливает желаемое состояние без предварительной очистки уцелевших объектов.

В план намеренно не входят:

- принудительный перехват клиентского DNS на портах 53/853 и блокировка DoH;
- управление IPv6 при `ipv6_enabled=false` — в этом режиме IPv6 осознанно остаётся unmanaged и может обходить PBR;
- периодический self-healing;
- отдельная CLI-команда `keen-pbr reload`;
- хранение версий списков или resolver-конфигураций на диске либо в памяти.

## Зафиксированные архитектурные решения

- Основная модель — desired-state reconciliation. Паттерн Strategy используется для реализаций iptables и nftables; Command не является основной абстракцией, но отдельные неизменяемые операции могут использоваться внутри плана для логирования и тестов.
- `RuntimeDesiredState` является единым описанием желаемых routing, firewall, resolver и conntrack объектов.
- `RuntimeReconciler` координирует общий `RoutingReconciler`, интерфейс `FirewallReconciler`, `ConntrackManager` и `ResolverCoordinator`.
- Operational CLI получает данные только от живого daemon через Unix domain socket. HTTP API и локальный IPC — адаптеры над одними application services.
- Обычные читатели видят только активный committed snapshot. Узкое исключение: во время config apply resolver endpoint может стримить candidate-конфигурацию процессу перезапуска dnsmasq.
- В нормальном reconcile запрещены глобальные `clear()`/`flush()` и удаление live-объекта до готовности его замены.
- `status` только наблюдает состояние и никогда не запускает исправление.

## Состояния runtime

| Состояние | Смысл | Resolver endpoint | Мутации |
|---|---|---|---|
| `starting` | daemon строит первое согласованное состояние | fallback | только внутренний startup |
| `running` | kernel, active config и dnsmasq согласованы | active | разрешены |
| `restart_required` | cache используемых списков новее активного runtime | fallback: `active_list_cache_mismatch` | reload/restart разрешены |
| `applying` | выполняется одна транзакция | candidate только для dnsmasq; прочим — previous/busy | прочие мутации получают `busy` |
| `stopped` | daemon жив, routing runtime остановлен | fallback: `runtime_stopped` | запуск/восстановление |
| `broken` | фактическое состояние не удалось согласовать или откатить | fallback: `runtime_broken` | только recovery-операции |
| `shutting_down` | выполняется контролируемая остановка | fallback | запрещены |

`broken` допускает диагностику, config save/apply, `download --reload`, REST refresh/restart и cache-only download. Cache-only download сам по себе не снимает `broken`.

## Этапы внедрения

1. Ввести модели desired/actual state, state machine и диагностические типы без смены поведения.
2. Реализовать routing и firewall reconcilers, безопасную замену наборов и ownership.
3. Ввести строгие guards, стабильное распределение mark/table/rule priority и conntrack stickiness.
4. Добавить DNS OUTPUT enforcement и транзакционный resolver/config apply.
5. Перевести списки на явный lifecycle и operational CLI на IPC.
6. Обновить packaging, документацию/UI и включить fault/integration-набор тестов.

---

## US-01. Единая модель desired/actual state и интерфейс reconciler

**Выполнено: да**

**Сложность:** высокая.

**Ожидаемая стабильность:** высокая после покрытия backend-интеграционными тестами; это фундамент остальных задач.

**Зависимости:** нет.

### Фактическое поведение

Routing, firewall, resolver и lifecycle меняются разными путями. Переиспользуемые command buffers могут сохранять незавершённые операции после ошибки. Код часто исходит из того, что ранее применённое состояние известно, вместо чтения фактических объектов ядра. Отдельные apply-пути сначала очищают состояние, а затем строят новое.

### Ожидаемое поведение

Ввести:

- `RuntimeDesiredState` — immutable snapshot желаемого состояния;
- backend-neutral модели routing rules/routes, firewall chains/rules/sets, resolver metadata и conntrack policy;
- `RuntimeActualState`, получаемый только inspect/probe операциями;
- `RuntimeReconciler`, который выполняет `inspect → plan → apply → inspect/verify → commit`;
- `FirewallReconciler` с операциями `probe`, `inspect`, `plan`, `apply`, `verify`, `cleanup`;
- реализации `IptablesReconciler` и `NftablesReconciler` как Strategy;
- attempt-scoped immutable plan, уничтожаемый полностью при успехе или ошибке.

`plan` может состоять из отдельных операций, но их нельзя накапливать в долгоживущем mutable command buffer. Normal reconcile выполняет минимальные изменения и не вызывает общий `clear()`.

### Критерии приёмки

- Один и тот же `RuntimeDesiredState` используется routing, firewall, resolver и conntrack подсистемами.
- Повторный reconcile уже согласованного состояния формирует пустой plan и не вызывает mutating-команды.
- Ошибка на любой операции не переносит оставшиеся операции в следующую попытку.
- После apply выполняется независимый inspect и проверка фактического состояния.
- Backend-specific детали атомарности находятся внутри соответствующей Strategy.
- `status` использует inspect, но не вызывает plan/apply/cleanup.
- Старые unconditional clear/flush отсутствуют во всех normal re-apply путях.

### Тест-кейсы

1. Дважды применить одинаковый desired state; во второй раз mutating syscall/command не выполняется.
2. Инъецировать ошибку в середине plan, затем применить новый plan; операции первой попытки не исполняются.
3. Изменить один route и убедиться, что firewall/resolver не пересоздаются.
4. Изменить только один static set и убедиться, что routing plan пуст.
5. Подменить фактическое состояние между apply и verify; reconcile сообщает drift и не публикует commit.
6. Вызвать `status` при drift; состояние отображается, но ядро остаётся неизменным.

**Примечания после имплементации:**

- Добавлены immutable backend-neutral snapshots `RuntimeDesiredState`/`RuntimeActualState`
  и attempt-scoped `RuntimeReconciler`: inspect → plan → apply → independent
  inspect/verify → commit. Незавершённый plan уничтожается при любой ошибке.
- Normal re-apply routing теперь строит dry-run desired state и выполняет
  add-missing → prune-obsolete; новые routes/rules устанавливаются до удаления
  старых owned объектов. `clear()` сохранён только для controlled shutdown/stop.
- Iptables PreserveSets использует стабильный dispatcher и versioned private
  chains: switch dispatcher выполняется до удаления старого chain в одном
  `iptables-restore` transaction. Nftables сохраняет собственную atomic batch
  strategy.
- Conntrack policy и resolver expected/actual hash вынесены в отдельные
  reconciler-visible managers; resolver actual обновляется только из probe.
- Добавлены unit tests для empty/failed/drift plans, status без мутаций,
  route/rule reconciliation, versioned iptables switch, conntrack и resolver
  coordinators. Полный unit suite и clangd-tidy без warnings проходят.

---

## US-02. State machine и сериализация runtime-операций

**Выполнено: да**

**Сложность:** высокая.

**Ожидаемая стабильность:** высокая; явные переходы устраняют гонки между apply, download и resolver reload.

**Зависимости:** US-01.

### Фактическое поведение

Разные entry points могут менять конфигурацию, списки и kernel state без единой транзакционной границы. Нельзя надёжно отличить active состояние от applying, partially failed или требующего restart. Ошибка apply может оставить смешанное состояние, не представленное в API.

### Ожидаемое поведение

Реализовать перечисленные выше состояния и единственный operation coordinator. Одновременно допускается не более одной mutating operation. Конкурирующая мутация немедленно получает структурированную ошибку `busy`, а не ждёт lock.

Публикация active snapshot происходит только после полной проверки kernel и dnsmasq. Во время `applying` обычные reads возвращают предыдущий committed snapshot либо `busy`; candidate доступен только внутренне авторизованному resolver-запросу текущей транзакции.

### Критерии приёмки

- Все переходы заданы таблицей и проверяются централизованно.
- Невозможный переход возвращает ошибку без изменения runtime.
- Config apply с успешным rollback возвращает `running` и прежний active snapshot.
- Ошибка rollback либо list-only reconcile переводит runtime в `broken`.
- Cache refresh используемого списка без apply переводит `running` в `restart_required`.
- Изменение только неиспользуемого списка оставляет `running`.
- Recovery-операции из `broken` явно перечислены и тестируются.
- Состояние и последняя причина перехода доступны в status/API.

### Тест-кейсы

1. Запустить config apply и параллельно `download --reload`; второй запрос сразу получает `busy`.
2. Во время applying прочитать status: виден previous active и признак ongoing operation, candidate не раскрывается.
3. Успешно завершить все фазы и проверить единственный переход `applying → running`.
4. Сломать candidate apply, успешно откатить old state и проверить `running` со старым snapshot.
5. Сломать apply и rollback; проверить `broken` и resolver fallback.
6. В `broken` вызвать диагностику и cache-only download — они работают, но состояние не становится `running`.
7. Вызвать запрещённую мутацию из `shutting_down`; получить стабильную protocol error.

**Примечания после имплементации:**

- Добавлен централизованный `RuntimeStateMachine` с таблицей допустимых
  переходов, transition reason и запретом недопустимых mutation transitions.
  Daemon публикует `runtime_state` и `runtime_state_reason` в committed
  `RuntimeStateSnapshot` и через `GET /api/health/service`.
- Config apply переводит runtime в `applying`; успех возвращает `running`,
  ошибка — `broken`. Rollback apply допускается из `broken` и при успехе
  возвращает previous active runtime в `running`.
- `OperationCoordinator` добавлен к API mutation guard: конкурентная операция
  немедленно получает busy/409, без ожидания mutex. Lease освобождается при
  finish и при failed runtime preconditions.
- Добавлены unit tests переходов, recovery/shutdown, state snapshot publication
  и immediate-busy coordinator. OpenAPI types регенерированы.

---

## US-03. Routing reconciler, ownership и проверяемое применение netlink-объектов

**Выполнено: да**

**Сложность:** высокая.

**Ожидаемая стабильность:** высокая для поддерживаемых ядер; ownership rules должны быть проверены на Keenetic/OpenWrt/Debian.

**Зависимости:** US-01, US-02.

### Фактическое поведение

Policy rules и routes могут удаляться до установки замены. Ошибки удаления местами теряются. Пустая policy table позволяет RPDB продолжить поиск в `main`, поэтому пакет уходит без туннеля. После crash daemon не всегда может однозначно отличить свои объекты от чужих. Table/mark identity зависит от порядка конфигурации.

### Ожидаемое поведение

`RoutingReconciler` читает rules/routes через netlink, вычисляет diff и применяет его без предварительной очистки. Generated interface/URLTEST routes получают выделенный числовой `rtm_protocol`, задаваемый build option. Дополнительные routes с этим protocol удаляются; чужие routes сохраняются. Конфликт чужого route с желаемым объектом завершает apply ошибкой с рекомендацией изменить `table_start`.

Rules считаются owned по эксклюзивному fwmark mask, поскольку rule protocol недоступен на части старых Keenetic kernels. Любое точное nonzero совпадение mark/mask в зарезервированном namespace можно adopt. Изменение mask при выключенном daemon не позволяет доказуемо найти старый namespace и должно быть документировано.

### Критерии приёмки

- Сначала создаются/заменяются новые lookup paths и guards, затем удаляются только доказанно лишние owned objects.
- Netlink replace используется там, где объект можно заменить in-place.
- Каждая ошибка add/replace/delete сохраняется в apply result и health.
- Verify обнаруживает отсутствующие, лишние и конфликтующие owned objects.
- Foreign routes/rules вне namespace не изменяются.
- После crash новый daemon inspect/adopt существующее состояние без manifest-файла.
- Mark/table/rule allocation детерминирован для неизменного active config.

### Тест-кейсы

1. Удалить один owned route вручную; reconcile добавляет только его.
2. Добавить лишний route с own `rtm_protocol`; reconcile удаляет его.
3. Добавить foreign route в той же таблице без own protocol; он сохраняется.
4. Создать foreign route, конфликтующий с desired; apply падает, active snapshot не меняется.
5. Инъецировать ошибку удаления; verify показывает extra object и операция не считается committed.
6. Перезапустить daemon после crash с целыми объектами; mutating diff пуст.
7. Поменять порядок независимых секций конфигурации без изменения их identity; проверить стабильную выдачу идентификаторов согласно заданному allocator contract.

**Примечания после имплементации:** Добавлен `RoutingReconciler`: он читает
kernel routes/rules, создаёт недостающие lookup paths до удаления устаревших,
adopt-ит совпадающие объекты после restart и удаляет routes только с выделенным
`rtm_protocol`. Protocol задаётся build option `KEEN_PBR_ROUTE_PROTOCOL`
(default 186). Конфликтующий foreign route останавливает apply до мутаций;
ошибки удаления пробрасываются. Rules считаются принадлежащими активному exact
non-zero mark/mask namespace. Добавлены unit-тесты ownership, conflict,
adoption и delete failure.

---

## US-04. Fail-closed guards, rule priorities и отдельные enforced marks

**Выполнено: да**

**Сложность:** высокая.

**Ожидаемая стабильность:** высокая после network-namespace тестов; используются стандартные terminal RPDB actions без искусственной sink table.

**Зависимости:** US-03.

### Фактическое поведение

Если lookup rule попадает в пустую таблицу, Linux продолжает RPDB lookup и использует `main`. Текущий kill-switch не всегда защищает все internal detours. Номера rules могут сдвигаться при включении strict mode. `table_start` смешивает смысл table ID и priority.

### Ожидаемое поведение

Для каждого routable outbound резервируется пара rule priorities:

1. `fwmark → lookup outbound table`;
2. следующий direct terminal action `unreachable` либо `blackhole` для того же fwmark/mask.

Для interface/table outbound вторая rule создаётся только при effective strict mode, но priority резервируется всегда. Для URLTEST strict обязателен и отключить его нельзя. Никакой `sink table 999` не создаётся.

Добавить:

- `daemon.strict_enforcement_action`, default `unreachable`;
- optional `outbound.strict_enforcement_action`, наследующий daemon value;
- значения `unreachable|blackhole`;
- `iproute.rule_priority_start`; `null`/отсутствие означает использовать `table_start`;
- отдельные enforced-detour marks для interface/table outbound, используемых DNS или list download; они смотрят в ту же таблицу, но всегда имеют terminal guard;
- URLTEST использует свой normal mark, поскольку он всегда strict.

Сначала распределяются пары обычных outbound, затем пары internal detours, чтобы detours не сдвигали normal priorities. Валидатор заранее проверяет вместимость mark mask, table range и priority range.

### Критерии приёмки

- При strict=false пустая таблица сохраняет текущее documented fail-open поведение для user traffic.
- При strict=true пакет не достигает `main`, а получает настроенный `unreachable` или `blackhole`.
- DNS и list downloads всегда fail-closed независимо от user strict mode.
- Включение strict mode не меняет priority следующих outbound.
- `rule_priority_start=null` воспроизводит allocation от `table_start`.
- User table contents никогда не меняются; daemon управляет только lookup/guard rules.
- URLTEST strict нельзя выключить schema/API/UI путём.
- Capacity error возникает до изменения kernel state.

### Тест-кейсы

1. Удалить default route из strict=false table: пакет продолжает в main — тест фиксирует осознанное поведение.
2. Повторить со strict=true/unreachable: получить terminal unreachable, WAN capture пуст.
3. Повторить с blackhole: пакет молча отбрасывается, WAN capture пуст.
4. Сравнить priorities до/после включения strict у первого outbound; последующие не меняются.
5. Проверить наследование daemon action и outbound override.
6. При strict=false оборвать interface, затем выполнить internal DNS/list request: он не уходит через main.
7. Исчерпать mask/priority range; validation падает до netlink apply.
8. Для user-owned table проверить, что routes остались byte-for-byte неизменными.

**Примечания после имплементации:** Добавлены terminal RPDB actions
`unreachable|blackhole`, configurable на daemon/outbound уровне. Allocator
резервирует стабильные пары priority от `iproute.rule_priority_start` (fallback
на `table_start`), а URLTEST и internal DNS/list detours всегда получают
lookup+terminal pair. Для internal traffic выделяется отдельный fwmark; list
downloads используют именно его. Марки и table allocation детерминированы по
identity. Netlink inspect/reconcile и health проверяют priority/action; daemon
adopt-ит verified desired snapshot для status. Добавлены unit-тесты guards,
allocator reorder, validation и adoption.

---

## US-05. Backend-neutral firewall contract и ownership namespace

**Выполнено: да**

**Сложность:** средняя–высокая.

**Ожидаемая стабильность:** высокая; контракт позволяет проверять обе реализации одинаковым conformance-набором.

**Зависимости:** US-01, US-02, US-04.

### Фактическое поведение

Firewall backends имеют разные неполные механизмы проверки. Лишние chains/rules/sets могут оставаться, порядок rules проверяется не везде, а ошибочная попытка способна загрязнить следующую. Cleanup иногда требует удалить live chain/table, создавая окно утечки.

### Ожидаемое поведение

Общий `FirewallReconciler` описывает результаты `probe`, `inspect`, `plan`, `apply`, `verify`, `cleanup`, но actual parsing и atomic plan остаются backend-specific. Inspect обязан возвращать ordered rules, jumps, set schema/family/timeout и различать static/dynamic elements.

Зарезервировать эксклюзивные имена:

- nftables: `table inet KeenPbrTable`;
- iptables chains/jumps с префиксом/namespace `KeenPbrTable` для PREROUTING и OUTPUT;
- ipset: `kpbr4_*`, `kpbr6_*`, `kpbr4d_*`, `kpbr6d_*`.

Все лишние объекты внутри namespace удаляются; объекты вне него не трогаются. Порядок rules является частью desired state.

### Критерии приёмки

- Оба backend проходят один conformance suite.
- Inspect отличает missing, extra, reordered и schema mismatch.
- Static и dnsmasq-populated dynamic sets моделируются отдельно.
- Cleanup удаляет только namespace `keen-pbr`.
- Crash adoption не требует файлового ownership manifest.
- Failed plan не изменяет cached actual/desired и не переиспользуется.
- API health показывает backend capability и итог verify.

### Тест-кейсы

1. Переставить два rules вручную; verify обнаруживает mismatch.
2. Добавить extra chain/set в зарезервированном namespace; reconcile удаляет его.
3. Добавить похожее имя вне namespace; объект сохраняется.
4. Изменить family/timeout set; inspect выдаёт schema mismatch.
5. Сломать backend apply, затем повторить с исправным desired; вторая попытка не содержит старых operations.
6. Запустить conformance suite для iptables и nftables с одинаковыми logical fixtures.

**Примечания после имплементации:** Реализован backend-neutral
`FirewallReconciler` с attempt-scoped plan и lifecycle
probe/inspect/plan/apply/verify/cleanup. Shared inspect model сохраняет ordered
rules, chains/jumps и schema sets (family, timeout, dynamic). Добавлены adapters
для iptables/ip6tables/ipset и nft JSON; общий diff обнаруживает missing/extra,
reorder и schema mismatch. Extra objects actionable только в точных namespaces
`KeenPbrTable*`/`kpbr4_*`/`kpbr6_*`/`kpbr4d_*`/`kpbr6d_*`; foreign lookalikes
сохраняются. Conformance unit fixtures покрывают обе backend формы.

---

## US-06. Атомарный iptables/ipset reconciler

**Выполнено: да**

**Сложность:** высокая.

**Ожидаемая стабильность:** средняя–высокая; зависит от фактических возможностей `iptables-restore`/ipset на целевых прошивках.

**Зависимости:** US-05.

### Фактическое поведение

iptables re-apply удаляет owned chain, затем создаёт её заново. Static ipset обновляется через flush/refill. В обоих случаях существует окно, когда classifier отсутствует либо множество пусто. Dynamic DNS-наборы могут быть уничтожены вместе с накопленными dnsmasq адресами.

### Ожидаемое поведение

- Обновлять static ipset по схеме `create temporary → load → swap → destroy old temporary name`.
- Никогда не flush live ipset перед наполнением замены.
- Не пересоздавать dynamic dnsmasq sets при обычном re-apply и сохранять их элементы/timeout metadata.
- Применять chains/rules/jumps одной транзакцией `iptables-restore --noflush`; если конкретная платформа не гарантирует нужную замену, использовать shadow chain и атомарное переключение jump после её полного построения.
- Управлять отдельными owned chains для PREROUTING и OUTPUT; обеспечивать ровно один jump в каждую точку подключения.
- Проверять итог через `iptables-save` и `ipset save/list`.

Перед выбором между direct restore и shadow chain сделать platform capability test на минимально поддерживаемых версиях. Результат теста зафиксировать в коде capability probe и документации, а не определять эвристикой при каждом apply.

### Критерии приёмки

- Во время static set refresh live canonical name всегда ссылается либо на old, либо на полностью загруженный new набор.
- Dynamic set и его элементы переживают SIGUSR1 и unrelated config apply.
- PREROUTING/OUTPUT jumps отсутствуют в duplicate-виде.
- Ошибка загрузки temporary set оставляет live set неизменным.
- Ошибка rules transaction оставляет старую live chain достижимой.
- Reconcile удаляет extra owned chains/sets только после безопасного cutover.
- Apply и verify не используют строку команды как доказательство успеха: проверяется actual state.

### Тест-кейсы

1. Непрерывно отправлять UDP во время обновления большого ipset; WAN capture не видит packet leak.
2. Инъецировать invalid element в temporary load; canonical set содержит старые элементы.
3. Заселить dynamic set через dnsmasq fixture, отправить SIGUSR1; элементы сохранены.
4. Прервать `iptables-restore`; старый jump/chain продолжает классифицировать трафик.
5. Создать duplicate jump; reconcile оставляет один jump правильной позиции.
6. Создать лишний owned set; он удаляется после successful cutover, foreign set сохраняется.
7. Проверить `--noflush`/shadow-chain путь на каждой поддерживаемой platform fixture.

**Примечания после имплементации:** Статические `kpbr4_`/`kpbr6_` наборы теперь
загружаются под generation-scoped временным именем и публикуются через `ipset
swap`; ошибка загрузки не меняет canonical set. Обычный `PreserveSets` не
пересоздаёт динамические `kpbr4d_`/`kpbr6d_` наборы. Добавлены отдельный
OUTPUT-dispatcher и переключение PREROUTING/OUTPUT на shadow chain в одной
`iptables-restore --noflush` транзакции. Базовая capability-договорённость и
ограничения задокументированы в `docs/content/docs/iptables-atomic-reconcile.md`.

---

## US-07. Атомарный nftables reconciler и корректная family-семантика

**Выполнено: да**

**Сложность:** высокая.

**Ожидаемая стабильность:** высокая благодаря atomic nft transaction; schema migration требует отдельных тестов старых nft.

**Зависимости:** US-05.

### Фактическое поведение

nft re-apply может сначала удалить live table, а затем загрузить новую. При ошибке между операциями enforcement отсутствует. Port-only rules могут получить неверную IPv4/IPv6 family-семантику. Пересоздание sets теряет dynamic DNS elements.

### Ожидаемое поведение

- Создавать весь nft diff одной atomic transaction без предварительного удаления live table.
- При совместимой schema обновлять static elements атомарно.
- При несовместимой schema создавать versioned replacement set, переключать rules на него в той же transaction и только затем удалять старый set.
- Не flush/recreate dynamic sets и сохранять learned elements.
- Явно строить IPv4 и IPv6 expressions для port-only rules; при `ipv6_enabled=false` IPv6 rules не создаются и обход остаётся documented.
- При включённом семействе capability probe не должен молча считать отсутствующий backend успешным enforcement; состояние отражается в health/apply result.

### Критерии приёмки

- Invalid nft transaction не изменяет предыдущую live table.
- Ни один normal path не выполняет standalone `delete table inet KeenPbrTable` до replacement.
- Dynamic elements сохраняются при SIGUSR1, list update и unrelated config apply.
- Port-only IPv4 rule не генерирует некорректный IPv6 match и наоборот.
- При `ipv6_enabled=false` status и документация явно показывают `IPv6 unmanaged`.
- Extra owned objects удаляются в той же безопасной transaction.

### Тест-кейсы

1. Добавить syntax error в transaction; сравнить nft ruleset до/после — идентичен.
2. Мигрировать set schema через versioned replacement под непрерывным UDP/TCP; утечки отсутствуют.
3. Заселить dynamic set, выполнить SIGUSR1; элементы и timeout сохранены.
4. Сгенерировать port-only IPv4/IPv6 fixtures и проверить exact nft AST/ruleset.
5. Запустить с `ipv6_enabled=false`; IPv4 работает, IPv6 не помечается, health содержит unmanaged indication.
6. Удалить требуемую nft capability; apply не публикует ложный `running` для включённого enforcement.

**Примечания после имплементации:** Normal apply больше не выполняет standalone
`delete table`: таблица, chain/rules и static-set refresh публикуются одной
JSON-транзакцией `nft -j -f -`. Existing static set очищается и наполняется в
batch; schema mismatch заменяется там же. Dynamic dnsmasq sets сохранены при
routine re-apply. Port-only правила разворачиваются по IPv4/IPv6, при
`ipv6_enabled=false` IPv6 не управляется. Ограничения и atomic-path описаны в
`docs/content/docs/nftables-atomic-reconcile.md`.

---

## US-08. Conntrack stickiness и безопасное переключение URLTEST

**Выполнено: да**

**Сложность:** очень высокая.

**Ожидаемая стабильность:** средняя–высокая; поведение NAT/conntrack существенно различается между kernels и требует реальных namespace/platform тестов.

**Зависимости:** US-03, US-04, US-05.

### Фактическое поведение

Каждый пакет классифицируется заново. При временной пустоте set, изменении routes или URLTEST candidate пакеты одного соединения могут получить другой mark либо уйти в main. Нет таргетированного lifecycle для conntrack entries, принадлежащих старым marks.

### Ожидаемое поведение

Сделать userspace `conntrack` обязательной package/runtime dependency, а kernel CONNMARK/ct-mark primitives — обязательной capability. Silent fallback без stickiness запрещён.

Для original direction:

1. если в ctmark есть keen-pbr bits — восстановить только зарезервированные bits в nfmark и пропустить повторную классификацию;
2. иначе классифицировать первый пакет;
3. сохранить в ctmark только успешно выбранный routed/DNS-detour mark под keen-pbr mask.

Не сохранять marks для ignore, unmatched и terminal blackhole. Reply direction не восстанавливать в routing mark. Чужие bits nfmark/ctmark сохранять.

User traffic к URLTEST и DNS detour через URLTEST используют mark самого URLTEST, не mark выбранного child. Candidate switch выполняется `RTM_NEWROUTE | NLM_F_REPLACE` в URLTEST table; guard остаётся постоянно. При отсутствии candidate unicast route отсутствует, guard блокирует.

Добавить `urltest.conntrack_on_switch: preserve|delete`, default `preserve`. Для `delete`: подготовить routes → replace/verify route → таргетированно удалить conntrack entries URLTEST mark → убрать старые routes. Ошибка cleanup выдаёт warning, но не переводит runtime в `broken`.

При live config change, меняющем mark map, best-effort удалить entries старых active marks через `conntrack -D -f ... --mark mark/mask`. То же сделать при graceful `stop_routing_runtime`/shutdown. На SIGUSR1 и list-only reconcile cleanup не выполнять. Если точное таргетирование невозможно, cleanup пропускается с warning.

### Критерии приёмки

- Один established flow сохраняет mark при static set swap и SIGUSR1.
- В одном направлении сохраняются только keen-pbr mask bits; чужие bits не меняются.
- Reply packets не отправляются повторно по PBR из-за restore.
- URLTEST switch с `preserve` не удаляет conntrack entries.
- URLTEST switch с `delete` удаляет только entries URLTEST mark после готовности нового route.
- Отсутствие userspace/kernel dependency не допускает успешного runtime apply.
- Graceful stop очищает текущие owned marks best-effort; crash не требует manifest и не вызывает широкого flush.
- Ограничение DNS TTL документировано: existing ctmarked flows живут, но новые connections к IP после expiry могут не совпасть с set.

### Тест-кейсы

1. Установить TCP/UDP flow, swap static set, проверить неизменный ctmark/nfmark и отсутствие разрыва.
2. Проверить original и reply directions с NAT; PBR mark восстанавливается только original.
3. Установить чужие mark bits до классификации; после save/restore они сохранены.
4. Переключить URLTEST в режиме preserve; flow и conntrack entry сохраняются.
5. Переключить в delete; новый route уже active до targeted deletion, foreign entries остаются.
6. Отключить candidate; WAN capture не показывает main fallback благодаря guard.
7. Изменить active mark map; проверить best-effort cleanup old marks и warning при mock failure.
8. Удалить binary/kernel capability; startup/apply завершается controlled error/broken, а не silent downgrade.
9. Выполнить SIGUSR1; targeted conntrack delete не вызывается.

**Примечания после имплементации:** В iptables и nftables добавлены
original-direction restore и save только fwmark bits из configured mask;
чужие bits сохраняются, reply не восстанавливается. URLTEST/DNS detour
используют стабильный mark URLTEST. Добавлен `conntrack_on_switch:
preserve|delete` (default preserve); delete выполняет targeted IPv4/IPv6
cleanup только после routing/firewall reconcile. Graceful stop также делает
best-effort targeted cleanup без global flush. Контракт и command shape
покрыты unit tests; failure cleanup остаётся warning.

---

## US-09. DNS fail-closed для forwarded и локального OUTPUT traffic

**Выполнено: нет**

**Сложность:** высокая.

**Ожидаемая стабильность:** высокая после проверки hook order и conntrack direction на обоих backend.

**Зависимости:** US-04, US-06/US-07, US-08.

### Фактическое поведение

Detour rules в PREROUTING не охватывают DNS-запросы, созданные локальными процессами роутера. Такой трафик может уйти через main. Даже совпавший DNS detour может использовать non-strict user mark и fail-open при исчезновении route.

### Ожидаемое поведение

- Сохранить PREROUTING detour для forwarded direct connections к каждому exact configured DNS server address:port.
- Добавить equivalent OUTPUT classifier для TCP и UDP. Он применяется к любому локальному процессу, а не только dnsmasq.
- Использовать enforced-detour mark interface/table либо normal always-strict URLTEST mark.
- Всегда завершать lookup terminal guard независимо от `daemon.strict_enforcement`.
- Сохранять чужие mark bits и интегрировать правило с original-direction conntrack restore/save.
- Не внедрять клиентский DNS redirect: обращения к другим DNS/DoT/DoH остаются вне scope и документируются.

### Критерии приёмки

- Локальный UDP/TCP запрос к configured endpoint никогда не выходит через main при недоступном outbound.
- Forwarded запрос к configured endpoint имеет ту же fail-closed гарантию.
- URLTEST endpoint использует mark URLTEST, а не текущего child.
- Любой локальный UID/process затрагивается одинаково.
- Запрос к неуказанному endpoint не перехватывается.
- PREROUTING и OUTPUT rules применяются атомарно вместе с остальным firewall state.

### Тест-кейсы

1. Отправить UDP и TCP DNS локально от dnsmasq fixture и отдельного процесса; оба используют detour.
2. Удалить detour route при strict=false user policy; configured DNS блокируется guard, WAN capture пуст.
3. Повторить для forwarded client request в PREROUTING.
4. Направить запрос к другому DNS/DoT/DoH endpoint; правило не совпадает.
5. Переключить URLTEST child; DNS flow использует стабильный URLTEST mark.
6. Проверить сохранение чужих nfmark/ctmark bits.
7. Инъецировать firewall transaction failure; старые PREROUTING и OUTPUT rules остаются согласованы.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## US-10. Жизненный цикл списков и безопасный list-only reconcile

**Выполнено: нет**

**Сложность:** очень высокая.

**Ожидаемая стабильность:** высокая после fault tests; raw-byte policy проста, но комбинация startup/download/reload требует строгой state machine.

**Зависимости:** US-02, US-04, US-06/US-07, US-09.

### Фактическое поведение

Скачивание, парсинг и применение списков связаны неявно. Config apply может неожиданно refresh существующий cache. Изменение cache не всегда отражается в runtime state. Flush наборов создаёт окно утечки, а ошибки отдельных списков и нераспознанные строки имеют неоднозначный результат.

### Ожидаемое поведение

Разделить операции:

- startup/config apply скачивает только полностью отсутствующие URL cache files, никогда не refresh существующие;
- обычный `download` делает full refresh cache, но не меняет runtime;
- `download --reload`, auto-update и REST refresh выполняют full refresh, затем list-only reconcile;
- все downloads выполняются atomically `temporary file → validate transport/write → rename`, с `SO_MARK` enforced detour после готовности routing guards;
- изменение определяется только hash raw bytes; HTTP metadata/timestamp и semantic result не считаются изменением;
- metadata записывается только при фактически полученном content, не при каждом startup/parse.

Parser применяется при построении desired state, а не при download. Неизвестные строки отбрасываются. Непустой файл с хотя бы одной распознанной записью успешен; неизвестные строки не ухудшают health. Empty/comment-only файл — valid empty. Непустой используемый файл с нулём распознанных записей переводит runtime в `broken`.

Обычный `download`:

- если raw bytes используемого списка изменились — `restart_required`;
- если изменился только неиспользуемый список — `running` сохраняется;
- resolver endpoint при `restart_required` выдаёт fallback `active_list_cache_mismatch`, потому что версии active cache отдельно не хранятся.

`download --reload` применяет list-only reconcile только если raw bytes хотя бы одного используемого списка изменились. Если runtime уже `broken`/unapplied, reconcile разрешён даже при 304/no change как recovery retry. При partial failure успешные relevant изменения применяются, failed lists сохраняют старый cache; команда возвращает nonzero/structured per-list errors.

List-only reconcile меняет только static set elements: ipset swap либо nft atomic element replacement. Routes/rules/chains не трогаются, dynamic sets сохраняются. dnsmasq restart выполняется только если изменённый список участвует в resolver generation. Если cache уже заменён, но reconcile/rollback не удался, cache не откатывается, runtime становится `broken`.

Corrupt/unreadable existing cache не удаляется заранее: выполнить unconditional download во временный файл и заменить только при успехе. Ошибка должна быть заметна в status/CLI, без повторной скрытой записи на диск.

### Критерии приёмки

- Config apply/startup никогда не refresh существующий cache.
- Missing cache скачивается с enforced `SO_MARK`; classifier user traffic до этого не активирован.
- Raw-identical content не вызывает write, restart или reconcile.
- `download` никогда не меняет kernel/resolver, но корректно выставляет `restart_required`.
- `download --reload`/auto-update/REST используют один application service и одинаковую семантику.
- Partial errors возвращаются пользователю, successful caches не откатываются.
- Unknown lines отбрасываются; all-unknown nonempty used file является broken; empty/comment-only valid.
- List-only reconcile сохраняет established flows и dynamic DNS elements.
- Missing used list + failed download оставляет daemon живым в `broken` с fallback `list_cache_missing`; unused missing list даёт warning и не мешает `running`.

### Тест-кейсы

1. На config apply иметь существующий remote cache; убедиться, что HTTP request отсутствует.
2. Удалить cache; startup скачивает его через SO_MARK после установки lookup/guard.
3. Вернуть те же raw bytes с новым ETag/time; файл не переписывается, apply не запускается.
4. Обычным download изменить used list; kernel прежний, state `restart_required`, resolver fallback.
5. Изменить unused list; state остаётся `running`.
6. `download --reload` изменить firewall-only list; выполнить только set swap, dnsmasq не restart.
7. Изменить resolver-used list; set swap и dnsmasq transaction выполняются.
8. Один из трёх downloads падает, два успешны; old failed cache сохранён, два new применены, exit nonzero с деталями.
9. Проверить files: mixed valid/unknown, all unknown, empty, comments-only.
10. Сделать cache unreadable/corrupt; successful temp download заменяет его один раз, failed download не уничтожает old file и виден пользователю.
11. Сломать reconcile после cache rename; runtime `broken`; повторный `download --reload` при 304 повторяет reconcile.
12. Во время list-only reconcile непрерывные TCP/UDP flows не меняют mark и не утекают.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## US-11. Unix socket IPC и daemon как единственный источник operational state

**Выполнено: нет**

**Сложность:** высокая.

**Ожидаемая стабильность:** высокая; versioned framing и единый service layer ограничивают расхождение CLI/HTTP.

**Зависимости:** US-02.

### Фактическое поведение

CLI-команды могут читать конфигурацию/состояние напрямую с диска и получать результат, не совпадающий с активным runtime daemon. API и CLI имеют разные пути исполнения. Параметр `--config` создаёт впечатление, что operational-команду можно направить на произвольный snapshot.

### Ожидаемое поведение

Добавить Unix domain `SOCK_STREAM` control socket и versioned length-prefixed JSON protocol с `request_id`, `operation`, `protocol_version`, `schema_version`, status/error envelope. HTTP и IPC handlers вызывают одни application services, а не дублируют business logic.

Через daemon выполняются:

- `status`;
- `test-routing`;
- `download` и `download --reload`;
- `resolver-config-hash`;
- `generate-resolver-config ...` с отдельной streaming-семантикой US-12.

Без daemon работают только `service`, `--help`, `--version`; resolver generation имеет специально описанный fallback. `--config` остаётся только для `service`, а operational-команды с ним завершаются понятной ошибкой.

Socket path задаётся build option:

- Debian/OpenWrt: `/run/keen-pbr/control.sock`;
- Keenetic: `/tmp/keen-pbr/control.sock`.

Mode `0660`, owner `root:keen-pbr`. Peer проверяется через filesystem permissions и `SO_PEERCRED`. Участники группы могут выполнять list update; матрица разрешений для read/mutate задаётся явно. Общий connect timeout — 1 секунда, non-streaming response idle/read timeout — 5 секунд.

### Критерии приёмки

- CLI не парсит active config/list runtime с диска.
- Остановка daemon делает operational-команды неработающими, кроме documented resolver fallback.
- CLI и HTTP для одной операции возвращают эквивалентные domain results/errors.
- Несовместимая protocol/schema version возвращает version error, а не неопределённое чтение.
- Socket создаётся с правильными path/owner/mode и удаляется безопасно при graceful shutdown.
- `SO_PEERCRED` проверяется до mutating operation.
- Параллельная mutation немедленно получает `busy`.
- `--config` у operational-команды не игнорируется молча.

### Тест-кейсы

1. Сравнить status через HTTP и IPC при running/restart_required/broken.
2. Остановить daemon; `status`, `test-routing`, `download` завершаются nonzero.
3. Передать operational-команде `--config`; получить usage error до обращения к файлу.
4. Подключиться несовместимой protocol version; получить structured error.
5. Проверить root, member `keen-pbr` и постороннего пользователя по permission matrix.
6. Запустить две mutations; вторая мгновенно получает busy.
7. Имитировать connect/read timeout; CLI завершается в заданные границы.
8. Проверить stale socket на startup: daemon безопасно определяет отсутствие live owner и переоткрывает только свой exact path.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## US-12. Streaming resolver config и fail-safe fallback

**Выполнено: нет**

**Сложность:** высокая.

**Ожидаемая стабильность:** высокая при строгом разграничении pre-stream и mid-stream failures.

**Зависимости:** US-02, US-11.

### Фактическое поведение

Resolver generator может использовать локальный disk config, проглатывать ошибки либо возвращать output, не соответствующий active daemon. Большие конфигурации неудобно целиком буферизовать. При недоступном daemon нет единой гарантированной fallback-семантики и диагностируемой причины.

### Ожидаемое поведение

CLI требует явный backend:

- `generate-resolver-config dnsmasq` — выбирает active firewall backend daemon;
- `dnsmasq-ipset` и `dnsmasq-nftset` остаются deprecated: warning в stderr, но генерация выполняется по старой явной backend-логике над active daemon data;
- отсутствие аргумента — usage error, не alias для `dnsmasq`.

Protocol: length-prefixed JSON header, затем raw byte chunks напрямую в stdout. Ни daemon, ни CLI не хранят весь output на диске, в RAM или memfd. Нет общего timeout; idle timeout между полученными chunks — 15 секунд и сбрасывается на каждом chunk. Время блокировки записи stdout не считается server idle. Resolver serving исполняется независимо от control/event loop, чтобы daemon мог одновременно ждать dnsmasq.

До первого active byte любая невозможность получить active config приводит к чтению статического fallback-файла, output и exit 0. После первого active byte ошибка останавливает stream и даёт nonzero без подмешивания fallback; dnsmasq conf-script должен отклонить partial output. Если fallback отсутствует/нечитаем — output отсутствует и exit nonzero.

Fallback paths — build options:

- Debian/OpenWrt: `/etc/keen-pbr/dnsmasq-fallback.conf`;
- Keenetic: `/opt/etc/keen-pbr/dnsmasq-fallback.conf`.

Каждая выдача содержит текущее время invocation и marker:

- active comments: `# keen-pbr resolver state: active`;
- active TXT: `txt-record=config-hash.keen.pbr,<timestamp>|<active-md5>` и `txt-record=resolver-state.keen.pbr,<timestamp>|active|runtime_active`;
- fallback comments: state и обязательная причина;
- fallback TXT: hash fallback content и `<timestamp>|fallback|<reason_code>`.

Стабильные reason codes минимум: `socket_unavailable`, `connect_timeout`, `protocol_error`, `stream_start_timeout`, `daemon_error`, `runtime_starting`, `runtime_stopped`, `runtime_broken`, `runtime_shutting_down`, `active_list_cache_mismatch`, `list_cache_missing`. Коды не содержат пробелов.

### Критерии приёмки

- `dnsmasq` обязателен как explicit argument.
- Deprecated backends предупреждают в stderr и выполняются с указанным backend, даже если active backend другой.
- Active data всегда приходит от daemon active/candidate resolver snapshot, не с диска CLI.
- Большой output стримится с bounded buffer.
- Fallback разрешён только до первого active byte.
- Timestamp обновляется на каждом invocation и отражает время генерации/restart, а не commit time.
- Runtime states выбирают согласованный active/fallback result и reason.
- Fallback file никогда не перезаписывается runtime helper-скриптами.

### Тест-кейсы

1. Вызвать без аргумента; получить usage error.
2. Вызвать deprecated backend; проверить warning и backend-specific output из active snapshot.
3. Удалить socket; fallback выдаётся с `socket_unavailable`, current timestamp, exit 0.
4. Перевести daemon в каждое runtime state; проверить active/fallback и reason.
5. Сломать stream до первого byte; выдан только fallback.
6. Сломать после первого byte; fallback не добавлен, exit nonzero.
7. Удалить fallback file и daemon; output пуст, exit nonzero.
8. Стримить конфиг больше memory test threshold; RSS/буфер остаются bounded.
9. Делать chunks каждые 14 секунд; timeout не срабатывает. Сделать паузу больше 15 секунд; срабатывает idle error.
10. Заблокировать stdout дольше 15 секунд при продолжающемся server progress; это не считается server idle.
11. Дважды вызвать генератор без изменения config; hash одинаков, timestamps различаются и близки к wall clock.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## US-13. Транзакционный config apply, dnsmasq handshake и durable commit

**Выполнено: нет**

**Сложность:** очень высокая.

**Ожидаемая стабильность:** средняя–высокая; стабильность зависит от исчерпывающей fault injection каждой фазы.

**Зависимости:** US-02, US-10, US-12, routing/firewall reconcilers.

### Фактическое поведение

API save может инициировать reload старого disk config до сохранения нового. Kernel, disk config и dnsmasq могут стать несогласованными. Resolver failure местами проглатывается. Проверка только hash без начала транзакции не доказывает, что dnsmasq действительно перезапустился на candidate config.

### Ожидаемое поведение

Config apply выполняется одной транзакцией:

1. принять и провалидировать candidate config;
2. запретить параллельный full list refresh;
3. построить routing tables/lookup/guards;
4. скачать только отсутствующие URL lists через enforced `SO_MARK`;
5. распарсить lists и построить полный candidate desired state;
6. reconcile/verify candidate kernel state;
7. открыть candidate только resolver endpoint текущей apply-транзакции;
8. вызвать platform dnsmasq restart/reload hook;
9. после возврата hook немедленно опрашивать TXT, затем примерно каждые 250 мс, пока не получены expected hash и timestamp не раньше начала apply; maximum 15 секунд после завершения hook;
10. atomically persist config: same-directory temp → fsync temp → rename → fsync parent;
11. atomically publish active snapshot и очистить draft.

Streaming времени hook не входит в 15 секунд. TXT polling и resolver stream не блокируют control/event loop.

При ошибке до commit disk остаётся старым. Daemon reconcile kernel и resolver обратно к previous active. Draft сохраняется. Успешный rollback возвращает `running`; failed rollback — `broken`. На первом startup без previous snapshot resolver failure оставляет daemon живым в `broken` для status/fallback.

Manual SIGHUP читает изменённый пользователем disk file и запускает ту же транзакцию. При failure runtime откатывается к old active, но внешний disk file не перезаписывается; health показывает `disk_config_mismatch`.

### Критерии приёмки

- До финального commit обычные readers видят previous active.
- dnsmasq получает candidate только внутри текущей applying transaction.
- Успех требует kernel verify, hook success, TXT expected hash и fresh timestamp.
- Poll завершается сразу при правильном TXT и не ждёт полные 15 секунд.
- Disk commit является crash-safe в пределах temp/fsync/rename/fsync-parent contract.
- API save не загружает старый disk config вместо candidate.
- Любая pre-commit ошибка вызывает полный rollback previous kernel+resolver.
- SIGHUP failure не затирает внешний disk file и явно показывает mismatch.
- Startup всегда перезапускает dnsmasq и проверяет fresh timestamp, даже если старый hash совпадает.

### Тест-кейсы

1. Успешный apply: TXT появляется через 300 мс; transaction завершается сразу, не ждёт 15 секунд.
2. Hook выполняется долго, затем TXT появляется быстро; timeout отсчитывается после hook return.
3. TXT содержит правильный hash, но старый timestamp; commit не происходит.
4. Инъецировать failure на validation, missing-list download, kernel apply, verify, hook, TXT poll, temp write, fsync, rename и parent fsync; проверить contract каждой фазы.
5. Во время apply читать CLI/HTTP status и resolver endpoint: обычный read previous, resolver для hook candidate.
6. Сломать candidate, успешно rollback; disk/active/kernel/dnsmasq old и state running, draft сохранён.
7. Сломать rollback; state broken и fallback reason.
8. Изменить disk вручную и отправить SIGHUP; success активирует его. Повторить с invalid apply; disk остаётся пользовательским, runtime old, mismatch виден.
9. Перезапустить daemon с уже совпадающим hash; hook всё равно вызывается, timestamp становится новым.
10. Убить процесс между каждой durable-write фазой и проверить recoverable disk result.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## US-14. Lifecycle: безопасный startup, SIGUSR1, SIGHUP и shutdown

**Выполнено: нет**

**Сложность:** высокая.

**Ожидаемая стабильность:** высокая после platform integration; внешнее удаление объектов Keenetic остаётся за пределами гарантии.

**Зависимости:** US-03–US-13.

### Фактическое поведение

Startup может активировать DNS/firewall до готовности routes и списков. SIGUSR1 использует destructive re-apply и сам создаёт disruption. Разные signal/API paths имеют различную семантику. Graceful stop не имеет точного порядка cleanup.

### Ожидаемое поведение

Startup order:

1. прочитать/валидировать config и existing caches;
2. проверить capabilities;
3. установить routing tables, lookup rules и terminal guards;
4. verify routing;
5. скачать только missing URL lists через SO_MARK, не активируя user classifier;
6. parse lists и построить final desired;
7. reconcile/verify firewall;
8. открыть resolver candidate;
9. всегда restart dnsmasq и verify TXT;
10. перейти в `running`.

SIGUSR1 выполняет только inspect/reconcile текущего committed desired kernel state. Он не reload config/list и не чистит conntrack. При intact kernel plan пуст, поэтому нет packet loss или дополнительного disruption. Если Keenetic уже удалил tables/rules, daemon восстанавливает missing objects; потери до сигнала/восстановления не считаются созданными daemon.

SIGHUP использует US-13. Interface event выполняет минимальный route reconcile. URLTEST event использует последовательность US-08. List events используют US-10. Graceful shutdown запрещает новые операции, targeted очищает current conntrack marks, затем удаляет только owned firewall/routing resources. При crash следующий startup inspect/adopt namespace.

### Критерии приёмки

- До routing verify ни user classifier, ни active resolver не доступны.
- Missing-list download никогда не идёт через main.
- SIGUSR1 при согласованном state не выполняет mutation.
- SIGUSR1 repair не удаляет уцелевшие objects перед добавлением missing.
- Signal handlers только ставят безопасное событие в coordinator, не выполняют сложную логику async-signal context.
- Shutdown не делает broad conntrack/firewall/routing flush.
- Crash/restart корректно adopts/removes deterministic extras.
- Helpers остаются малыми platform restart/reload adapters и не подменяют active config files.

### Тест-кейсы

1. Packet capture на startup: до полной готовности нет classifier-induced WAN leak.
2. SIGUSR1 при intact state под continuous UDP/TCP: zero additional mutations, drops и leaks.
3. Вручную удалить route/rule, затем SIGUSR1: missing восстановлен, уцелевшие не пересозданы.
4. Имитировать удаление Keenetic до SIGUSR1; отчёт отделяет external disruption от repair window.
5. Отправить серию SIGUSR1 во время apply; события coalesce/serialize без concurrent mutation.
6. SIGHUP проходит config transaction, а не старый clear/reload path.
7. Graceful stop очищает только exact current marks и owned namespace.
8. SIGKILL после разных apply phases, затем startup; inspect/adopt приводит к единому verified state.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## US-15. Read-only status, health и диагностируемые ошибки

**Выполнено: нет**

**Сложность:** средняя.

**Ожидаемая стабильность:** высокая; read-only снимок не влияет на enforcement.

**Зависимости:** US-01, US-02, US-11.

### Фактическое поведение

Пользователь не всегда видит, что cache новее runtime, disk отличается от active, удаление объекта не удалось либо resolver работает на fallback. Некоторые resolver/apply ошибки только логируются или теряются. Status может опираться на предполагаемое, а не фактическое состояние.

### Ожидаемое поведение

`status` строит immutable snapshot из active metadata и read-only inspect. Он показывает минимум:

- runtime state и текущую/последнюю operation;
- active config identity/hash и `disk_config_mismatch`;
- backend/capability results;
- routing lookup/terminal guard actual state;
- PREROUTING/OUTPUT attachment и firewall verify;
- conntrack capability и выбранную policy;
- resolver active/fallback, reason, expected/observed TXT hash/time;
- `restart_required` и raw-hash changed list names;
- missing/unparseable used lists;
- last apply/rollback/cleanup error с phase и объектом;
- фактический drift/extra owned objects.

Никакая status/test-routing операция не запускает reconcile. Ошибки возвращаются через одинаковые structured domain codes в HTTP и IPC, сохраняются в health до следующего успешного релевантного действия и не маскируются exit 0, кроме явно определённого resolver fallback.

### Критерии приёмки

- Status не вызывает mutating syscalls/commands.
- Предполагаемое desired и observed actual различаются в ответе при drift.
- Failed delete/verify, resolver reason и per-list download errors доступны пользователю.
- `restart_required` перечисляет только использованные raw-changed lists.
- `disk_config_mismatch` имеет понятный active/disk контекст без публикации staged config как active.
- HTTP и IPC используют стабильные одинаковые error codes.
- Broken daemon остаётся диагностируемым.

### Тест-кейсы

1. Удалить route/firewall rule вручную и вызвать status; drift виден, объект не восстанавливается.
2. Сломать delete; проверить phase/object/error в health.
3. Изменить used cache обычным download; status показывает restart_required и имя списка.
4. Провалить SIGHUP после external disk change; показать disk mismatch и old active identity.
5. Перевести resolver на каждый fallback reason; status и TXT согласованы.
6. Сравнить error code HTTP/IPC для busy, broken, permission denied и validation failure.
7. После успешного релевантного recovery проверить очистку stale error при сохранении warning history согласно выбранной retention policy.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## US-16. Схема, API/UI, packaging и эксплуатационная документация

**Выполнено: нет**

**Сложность:** средняя–высокая.

**Ожидаемая стабильность:** высокая; основная опасность — миграция существующих конфигураций и различия пакетов платформ.

**Зависимости:** US-04, US-08, US-11, US-15.

### Фактическое поведение

Конфигурация не выражает terminal action, отдельный RPDB priority base и conntrack policy URLTEST. Пакеты не гарантируют `conntrack`, группу/socket permissions и fallback file. Документация недостаточно явно описывает first-match, отсутствие автоматического failover и IPv6/DNS bypass boundaries.

### Ожидаемое поведение

Обновить source schema/OpenAPI и выполнить codegen, не редактируя generated files вручную. Добавить:

- `daemon.strict_enforcement_action: unreachable|blackhole`, default `unreachable`;
- nullable `outbound.strict_enforcement_action` с наследованием;
- nullable `iproute.rule_priority_start`, fallback к `table_start`;
- `urltest.conntrack_on_switch: preserve|delete`, default `preserve`;
- runtime/health response fields US-15;
- build options для socket path, fallback path и numeric `rtm_protocol`.

Обновить UI для новых параметров и read-only runtime states/warnings. После изменения API выполнить `make generate`, а при изменении frontend API — `make frontend-api-generate`; frontend использует bun/bunx.

Packaging:

- добавить userspace `conntrack` как обязательную dependency;
- создать system group `keen-pbr`, настроить `root:keen-pbr` и `0660` socket;
- установить статический fallback config по platform path;
- оставить restart helpers маленькими адаптерами без файлового swap active config;
- обеспечить корректные runtime directory ownership/cleanup.

Документация обязана явно объяснить:

1. Два одинаково совпадающих правила не являются failover. Для `dst=1.1.1.1` первое правило ставит mark iface1 и завершает chain; правило iface2 недостижимо. Если iface1 исчез, strict=false оставляет его table без route, RPDB продолжает поиск в main и трафик уходит default gateway. На iface2 он не переключается. Strict=true terminal guard блокирует. Настоящий failover — один URLTEST outbound с iface1/iface2 candidates.
2. `unreachable` и `blackhole` отличаются наблюдаемым поведением приложений/игр.
3. `ipv6_enabled=false` означает unmanaged IPv6 bypass, не IPv6 block.
4. Keen-pbr не перехватывает произвольный клиентский DNS/DoT/DoH; клиенты должны использовать router resolver либо отдельные firewall controls.
5. Active config mark identity стабильна только пока конфигурация неизменна; offline смена fwmark mask после crash не позволяет найти старый namespace.
6. TTL limitation dynamic DNS sets: existing conntrack flow сохраняется, новый connection после expiry может не совпасть.
7. Plain `download` может потребовать restart; `download --reload` применяет сразу.

### Критерии приёмки

- Старые конфигурации без новых полей получают documented defaults.
- Invalid enum/range/mask capacity отвергаются до runtime mutation.
- Generated backend/frontend types соответствуют source schema.
- UI корректно показывает inheritance, mandatory URLTEST strict и restart_required/broken.
- Каждый platform package устанавливает dependency/group/fallback/path с нужными permissions.
- Upgrade не перезаписывает пользовательский fallback без package-manager policy/явной миграции.
- Руководство содержит точный duplicate-rule сценарий и границы гарантий.
- Deprecated resolver backend arguments отражены в help и выдают warning.

### Тест-кейсы

1. Загрузить старый config без новых полей; проверить defaults и стабильную миграцию.
2. Проверить schema/API/UI round-trip каждого нового значения и `null` inheritance.
3. Запустить generated-file consistency check после codegen.
4. Собрать/install-test пакеты Debian/OpenWrt/Keenetic; проверить conntrack dependency, group, socket и fallback path/mode.
5. Upgrade package с пользовательски изменённым fallback; проверить выбранную package conffile policy.
6. UI snapshot/e2e для running, restart_required, applying, broken и IPv6 unmanaged.
7. Выполнить команды из документации duplicate-rule/URLTEST в namespace fixture; наблюдаемое поведение совпадает с текстом.
8. Проверить CLI help для required `dnsmasq` и deprecated arguments.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## US-17. Интеграционный, fault-injection и непрерывный leak/disruption test suite

**Выполнено: нет**

**Сложность:** очень высокая.

**Ожидаемая стабильность:** высокая ценность, средняя первоначальная стабильность CI из-за timing/network namespaces; тесты должны использовать наблюдаемые barriers, а не произвольные sleep.

**Зависимости:** все предыдущие user stories.

### Фактическое поведение

Unit tests отдельных builders не доказывают отсутствие короткого fail-open окна, сохранность conntrack и атомарность kernel operations. Нет единого набора, непрерывно измеряющего packets на protected и main interfaces при SIGUSR1, list update, URLTEST switch и отказах apply.

### Ожидаемое поведение

Создать многоуровневый suite:

- unit/property tests allocation, ownership, diff, state transitions и exact backend order;
- contract tests обеих `FirewallReconciler` Strategy;
- Linux network namespace integration для iptables и nftables, IPv4 и управляемого IPv6;
- continuous UDP/TCP generators с sequence IDs, серверными логами и packet capture на tunnel/main WAN;
- conntrack/NAT tests original/reply direction;
- resolver IPC streaming/backpressure/fallback tests;
- config/list transaction fault injection на каждой фазе;
- crash/restart tests с kill points и adoption;
- package/platform smoke tests.

Для каждого сценария отчёт отдельно считает:

- leaked packets на main WAN;
- dropped/lost packets;
- duplicated/reordered packets при необходимости;
- длительность disruption;
- kernel mutations, созданные самим daemon.

Acceptance gate для SIGUSR1 при intact state: ноль дополнительных mutations, leaks и drops. Для config apply disruption допустим, но leak через main при effective strict/enforced detour — нет. Для external Keenetic pre-deletion результат помечается отдельно и проверяет только отсутствие дополнительного destructive gap в repair.

### Критерии приёмки

- Оба firewall backend проходят одинаковые logical leak scenarios.
- Test harness отличает leak от допустимого drop/blackhole.
- Нет timing-only assertions вида фиксированного sleep без readiness condition.
- Fault injection покрывает каждую apply/rollback/durable-write phase.
- SIGUSR1 intact-state gate выполняется детерминированно.
- List swap и URLTEST preserve не разрывают established flows в поддерживаемой модели.
- Test artifacts содержат rules/routes/conntrack snapshot и pcap при failure.
- CI явно разделяет быстрые unit, privileged namespace и platform/package jobs.

### Тест-кейсы

1. Непрерывный UDP game-like поток + SIGUSR1 на intact state: zero daemon-induced drops/leaks.
2. Тот же поток при заранее удалённой Keenetic table: измерить external gap; repair не делает дополнительный clear.
3. TCP/UDP при ipset swap и nft atomic replacement: zero main-WAN leaks, established flow сохраняется.
4. Config apply success/failure/rollback под трафиком: strict/enforced traffic не утекает.
5. Interface down при strict=false и strict=true: подтвердить documented fail-open и fail-closed различие.
6. URLTEST preserve/delete, candidate loss и route replace под NAT.
7. Local/forwarded DNS TCP/UDP при исчезновении outbound: main-WAN leaks равны нулю.
8. Resolver large stream, slow consumer, pre-byte failure и mid-stream failure.
9. Crash после каждого reconcile/dnsmasq/disk commit barrier; restart достигает verified running либо объяснимого broken.
10. Foreign object coexistence, owned extra cleanup и mark-mask capacity/property fuzzing.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## US-18. Исправить удаление списка в Web UI без удаления несвязанных правил

**Выполнено: нет**

**Сложность:** низкая–средняя.

**Ожидаемая стабильность:** высокая после unit-тестов преобразования draft-конфигурации и Web UI e2e-теста.

**Зависимости:** может быть реализована независимо; при изменении frontend-моделей в US-16 должна использовать их итоговое представление ссылок на списки.

### Фактическое поведение

При удалении списка в Web UI правила обрабатываются инвертированно:

- правила, которые не используют удаляемый список, удаляются из конфигурации;
- правила, которые используют удаляемый список, сохраняются, а ссылка на список из них удаляется.

В результате пользователь теряет несвязанные правила, хотя удаление списка не должно на них влиять.

### Ожидаемое поведение

При удалении списка Web UI должен выполнить ограниченное каскадное изменение draft-конфигурации:

- удалить только выбранный список;
- оставить все не использующие его правила без изменений;
- в каждом использующем его правиле удалить только ссылку на этот список;
- сохранить само правило и все остальные его поля и ссылки на другие списки, если после удаления ссылки в правиле остаётся хотя бы одно условие;
- удалить правило, если удаляемый список был его последним условием и после удаления ссылки правило больше ни по чему не может совпасть.

Сопоставление должно выполняться по стабильному идентификатору списка, а не по позиции в массиве или отображаемому имени. Изменение остаётся в draft до обычного сохранения конфигурации. Ошибка сохранения в API не должна закреплять optimistic state как успешно сохранённый active state.

### Критерии приёмки

- Правила без ссылки на удалённый list ID семантически идентичны состоянию до операции, включая порядок.
- В связанных правилах исчезает только удалённый list ID.
- Если правило использовало несколько списков, остальные ссылки сохраняются в прежнем порядке.
- Если после удаления ссылки остаётся хотя бы одно другое условие, правило сохраняется и проходит frontend/API validation.
- Если удаляемый список был последним условием правила, правило удаляется из draft.
- Количество правил уменьшается только на число связанных правил, которые после удаления списка остались без условий.
- Удаляется ровно один выбранный список; одноимённые списки с другим ID не затрагиваются.
- Preview/draft, отправляемый API payload и состояние после повторной загрузки страницы согласованы.
- При отказе API пользователь видит ошибку, а UI не сообщает, что изменение сохранено в active config.

### Тест-кейсы

1. Удалить список, который не используется ни одним правилом: список исчезает, все правила остаются без изменений.
2. Удалить список из правила, в котором есть другое условие: правило остаётся, удаляется только соответствующая ссылка.
3. Удалить список, являющийся единственным условием правила: правило удаляется.
4. У правила есть удаляемый и ещё два списка: два остальных ID и их порядок сохраняются.
5. Удалить список, используемый несколькими правилами: правила с другими условиями сохраняются, а правила без оставшихся условий удаляются.
6. В конфигурации одновременно есть связанные и несвязанные правила: ни одно несвязанное правило не удаляется.
7. Создать два списка с одинаковым отображаемым именем и разными ID: изменяется только выбранный ID.
8. Проверить reducer/state helper на immutable update: исходный draft object не мутируется.
9. Сохранить изменение, перечитать конфигурацию из daemon и перезагрузить страницу: результат не меняется.
10. Имитировать ошибку API save/apply: отображается ошибка, active snapshot остаётся прежним, повторная попытка возможна без дополнительной потери правил.

**Примечания после имплементации: (заполнить после выполнения user-story)**

---

## Матрица покрытия находок из исследования

| Находка `RESEARCH.md` | Основные user stories | Итоговое решение |
|---|---|---|
| 1. Empty/missing policy table fails open | US-04, US-17 | Lookup + direct terminal RPDB action; strict configurable, internal detours always strict |
| 2. Rules/routes очищаются до replacement | US-01, US-03, US-14 | Inspect/diff/replace/verify, без normal clear |
| 3. Dynamic DNS sets уничтожаются | US-05, US-06, US-07, US-10 | Dynamic sets preserved; static ipset swap/nft atomic replace |
| 4. Нет conntrack stickiness | US-08, US-17 | Masked ctmark save/restore original direction |
| 5. iptables chain удаляется перед созданием | US-06 | Atomic restore либо проверенный shadow-chain cutover |
| 6. nft table pre-delete | US-07 | Одна atomic transaction без pre-delete |
| 7. DNS detour не охватывает OUTPUT | US-09 | Exact configured endpoint TCP/UDP в PREROUTING и OUTPUT |
| 8. API save reload старого disk config | US-13 | Candidate transaction, commit disk только после kernel+TXT verify |
| 9. Resolver failures проглатываются | US-12, US-13, US-15 | Структурированные ошибки; fallback только до первого active byte |
| 10. DNS активируется до runtime на boot | US-10, US-13, US-14 | Routing/guards и lists до firewall/resolver; обязательный TXT handshake |
| 11. IPv6 bypass и nft port-only bug | US-07, US-16, US-17 | Исправить family rules; `ipv6_enabled=false` явно unmanaged |
| 12. Failed apply загрязняет buffers | US-01, US-05 | Immutable attempt-scoped plans |
| 13. Ошибки route/rule deletion теряются | US-03, US-15 | Все netlink results + post-apply actual verify |
| 14. Crash/restart ownership | US-03, US-05, US-14 | Deterministic namespace, rtm_protocol/fwmark ownership, inspect/adopt |
| 15. Клиенты могут обходить router DNS | US-09, US-16 | Не перехватывать в этой итерации; явно документировать boundary |
| 16. Marks/table IDs зависят от порядка | US-03, US-04, US-08 | Детерминированный allocator, pairs, отдельные table/rule bases, active-config guarantee |

## Рекомендуемый порядок поставки и контрольные точки

1. **Foundation:** US-01, US-02, US-15. Контрольная точка — новый status может показать desired/actual drift, но старый runtime path ещё допустим за feature flag.
2. **Kernel safety:** US-03–US-07. Контрольная точка — SIGUSR1 использует reconcilers и проходит zero-destructive-gap backend tests.
3. **Flow/DNS enforcement:** US-08, US-09. Контрольная точка — conntrack и OUTPUT tests проходят на обоих backend.
4. **Transactions and source of truth:** US-10–US-14. Контрольная точка — daemon/CLI/dnsmasq/disk publish только согласованные snapshots.
5. **Productization:** US-16, US-18, US-17. Контрольная точка — packages, migration, UI/docs, исправление удаления списков и privileged leak gate готовы к релизу.

Feature flags на промежуточных этапах не должны позволять смешивать новый state machine с destructive old re-apply в production. Переключение каждого backend выполняется целиком после прохождения его conformance и leak suite.

## Definition of Done для всей инициативы

- Все user stories имеют `Выполнено: да` и заполненные post-implementation notes с commit/PR, отклонениями и фактическими тестами.
- Operational CLI не имеет локального альтернативного источника active state.
- Normal reconcile не содержит flush/pre-delete gaps.
- DNS/list internal traffic всегда fail-closed; user traffic следует явной strict policy.
- SIGUSR1 на intact kernel state не создаёт mutations, packet loss или leak.
- Config/list operations имеют определённые commit/rollback/broken semantics.
- Resolver active/fallback состояние проверяемо через TXT и status.
- iptables и nftables проходят общий privileged leak/fault suite.
- Upgrade/migration и platform packages проверены на поддерживаемых Debian/OpenWrt/Keenetic targets.
