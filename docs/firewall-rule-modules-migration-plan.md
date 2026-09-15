# План миграции к модульным firewall-правилам

## 1. Назначение и границы

Этот план относится к ветке `feature/load-balancing` в состоянии commit
`cd616d61c3e015a8b7d5a50e9e04c13a77f7767f` от 15 сентября 2026 года.

Цель миграции — сделать firewall policy модульной, сохранив единую транзакцию
применения и существующую backend-specific lifecycle-механику.

Итоговое разделение ответственности:

- rule module определяет условие появления policy и формирует canonical rule;
- plan хранит полный упорядоченный desired firewall state;
- backend компилирует plan в iptables/ipset или nftables и публикует его одной
  транзакцией;
- inspector один раз читает фактическое состояние kernel firewall;
- общий verifier сравнивает canonical desired и observed rules;
- daemon определяет момент reconcile и публикует runtime/API state только после
  успешного применения.

Миграция не включает:

- `dlopen()` и ABI для внешних `.so` plugins;
- отдельный plugin для каждого matcher (`proto`, port, address, DSCP, set);
- переписывание iptables A/B generations;
- изменение семантики `FirewallApplyMode`;
- смену формата пользовательской конфигурации или API без отдельной
  необходимости;
- одновременное переписывание обоих backend-классов и health checker.

## 2. Неподвижные инварианты

Каждый PR обязан сохранять следующие свойства.

### 2.1. Транзакционность

- Rule module никогда не запускает `iptables`, `ip6tables`, `ipset` или `nft`.
- Nftables продолжает отправлять единый batch.
- Iptables продолжает готовить неактивную A/B generation, проверять её и затем
  переключать dispatcher.
- Ошибка подготовки или применения не публикует новый active/runtime state.

### 2.2. Apply modes

- `Destructive` пересоздаёт owned state согласно текущему контракту.
- `PreserveSets` сохраняет содержимое совместимых sets.
- `StaticSetsOnly` не затрагивает dynamic DNS contents вне текущего контракта.
- `RulesOnly` не создаёт и не наполняет sets; неуспешный preflight по-прежнему
  приводит к единственному контролируемому fallback в `PreserveSets`.
- Решение о режиме применения остаётся вне rule modules.

### 2.3. Ownership и безопасность cleanup

- Удаляются только объекты namespace keen-pbr.
- Comment ID не является единственным доказательством корректности rule:
  проверяются также hook/family/match/action.
- Неизвестный или повреждённый comment не расширяет область cleanup.
- Временные chain names, nft handles и активная A/B generation не входят в
  semantic identity.

### 2.4. Порядок правил

- Порядок является частью semantics и сравнивается как последовательность.
- Используется детерминированный порядок независимо от порядка `map`, адресов
  объектов и линковки translation units.
- Порядок route rules из конфигурации сохраняется.
- Expansion по family/protocol/list target также имеет явно зафиксированный
  порядок.

### 2.5. Backend parity

- Одинаковый canonical intent компилируется в эквивалентное поведение обоих
  backend'ов, если capability поддерживается.
- Nft-only functionality отклоняется до mutation kernel state.
- Нельзя предполагать, что iptables и nftables имеют одинаковые chain/hook,
  normalization или атомарность.

### 2.6. Runtime state

Для каждого apply различаются:

1. persisted config — принятая конфигурация;
2. staged inputs — подготовленные lists, sets, marks и balance candidates;
3. desired plan — canonical state, который требуется применить;
4. actual system state — результат inspection kernel;
5. active runtime state — последний успешно применённый plan;
6. API-visible state — projection только из active state;
7. failed candidate — plan, который не был опубликован после ошибки.

Новый desired plan становится active и API-visible только после успешного
backend apply. При ошибке остаётся доступен предыдущий active plan.

## 3. Целевая минимальная модель

Модель вводится постепенно. На первом проходе не нужен общий expression AST и
не нужен capability framework с десятками флагов.

### 3.1. Identity

```cpp
struct FirewallRuleKey {
    std::string module_id;
    std::string instance_id;

    std::string comment() const;
};
```

Требования:

- `module_id` — стабильное имя policy, например `route.mark`;
- `instance_id` строится из semantic inputs;
- serialization имеет версию, например `kpbr:v1:<module>:<instance>`;
- значения валидируются по разрешённому алфавиту и максимальной длине;
- если читаемость превышает лимит, `instance_id` хранит стабильный digest от
  canonical serialization, а человекочитаемые детали остаются в diagnostics;
- hash не использует `std::hash`, поскольку его стабильность между сборками не
  является контрактом.

### 3.2. Canonical action

На первом этапе достаточно существующих действий:

```cpp
struct MarkAction {
    uint32_t value{0};
    uint32_t mask{0xFFFFFFFFu};
};

struct BalanceAction {
    uint32_t fallback_mark{0};
    std::vector<uint32_t> candidate_marks;
};

enum class VerdictAction : uint8_t { drop, pass };

using FirewallRuleAction =
    std::variant<MarkAction, BalanceAction, VerdictAction>;
```

Не добавлять speculative actions. Restore-ctmark и ранние prefilter operations
можно временно оставить отдельным prefilter payload, пока их реальная backend
семантика не будет описана characterization tests.

### 3.3. Canonical rule

```cpp
enum class FirewallRuleStage : uint16_t {
    restore_conntrack = 100,
    global_bypass = 200,
    dns_detour = 300,
    route_classification = 400,
    terminal = 500,
};

enum class FirewallHook : uint8_t { prerouting, output };
enum class FirewallFamily : uint8_t { ipv4, ipv6 };

struct FirewallRuleInstance {
    FirewallRuleKey key;
    FirewallRuleStage stage{FirewallRuleStage::route_classification};
    int priority{0};
    std::size_t insertion_order{0};
    FirewallHook hook{FirewallHook::prerouting};
    FirewallFamily family{FirewallFamily::ipv4};
    FirewallRuleCriteria criteria;
    FirewallRuleAction action;
};
```

`insertion_order` назначает registrar. Он нужен как последний стабильный ключ
после `stage` и `priority`; полагаться только на `stable_sort` и случайный порядок
регистрации нельзя.

### 3.4. Plan

```cpp
struct FirewallPlan {
    std::vector<FirewallRuleInstance> rules;
    std::vector<FirewallSetDeclaration> sets;
    FirewallGlobalPrefilter global_prefilter; // временный compatibility payload
    uint32_t fwmark_mask{0xFFFFFFFFu};
};
```

На первом этапе plan не обязан владеть содержимым list streams: достаточно
деклараций и существующих loader callbacks/inputs. Перенос наполнения sets имеет
смысл только если он устраняет конкретное дублирование.

### 3.5. Modules

Начальный интерфейс должен быть минимальным:

```cpp
class FirewallRuleModule {
public:
    virtual ~FirewallRuleModule() = default;
    virtual std::string_view id() const noexcept = 0;
    virtual void register_rules(const FirewallBuildContext&,
                                FirewallRuleRegistrar&) const = 0;
};
```

Не добавлять `check()` в каждый module до появления общего snapshot verifier.
По умолчанию canonical comparison должен покрыть все обычные modules. Custom
check добавляется только для доказанного backend normalization case.

Registry — явный ordered manifest. Static constructor registration и macros не
используются.

## 4. Стратегия поставки

Каждый этап ниже — самостоятельный PR. PR не должен одновременно менять
canonical semantics, backend lifecycle и health output. После любого PR ветку
можно выпускать без обязательного продолжения миграции.

## PR 0. Зафиксировать текущее поведение

### Цель

Создать минимальную regression-сетку перед изменением representation. Production
code не менять.

### Работа

1. В `tests/test_firewall_runtime.cpp` покрыть таблицей существующую expansion:
   - mark/drop/pass;
   - IPv4/IPv6;
   - `L4Proto::Any`, TCP и UDP;
   - port с implicit protocol expansion;
   - source/destination addresses и negation;
   - list-backed static и dynamic sets;
   - `apply_output`;
   - default gateway;
   - balance candidates;
   - DNS detour TCP/UDP;
   - отсутствие rule для неактивного/пустого случая.
2. Зафиксировать порядок вызовов fake `Firewall`, а не только их количество.
3. В `tests/test_firewall_verifier.cpp` добавить пары fixture'ов, показывающие,
   что оба verifier'а принимают output, сгенерированный текущими backend rules,
   и отклоняют изменения action/mask/family/port.
4. В `tests/test_firewall_reconciler.cpp` зафиксировать ordered rule drift,
   owned extras и namespace boundary.
5. Добавить focused cases для всех четырёх apply modes, особенно запрет set
   streaming в `RulesOnly` и fallback после `FirewallRulesOnlyError`.

### Проверка

```sh
rtk make test
```

Если полный `make test` зависит от окружения, сначала запускать собранный
`cmake-build-gcc/tests/keen-pbr-tests` с фильтром doctest, затем документировать
конкретное environmental limitation.

### Критерий приёмки

- Production diff отсутствует.
- Тесты фиксируют точный порядок и семантику текущего output.
- Ни один тест не обращается к реальному firewall хоста.

## PR 1. Ввести RuleKey и RuleInstance как value types

### Цель

Добавить canonical identity без изменения вызовов backend и kernel output.

### Работа

1. Добавить один header и при необходимости один `.cpp`, например:
   - `src/firewall/firewall_rule.hpp`;
   - `src/firewall/firewall_rule.cpp` только для non-trivial serialization и
     validation.
2. Реализовать `FirewallRuleKey`, stage/hook/family, actions и
   `FirewallRuleInstance`.
3. Использовать существующие `FirewallRuleCriteria`, `PortSpec`, enums и mark
   types; не создавать параллельные matcher-типы.
4. Реализовать deterministic `comment()` и parser отдельно, но пока не включать
   comment в backend output.
5. Проверить:
   - round trip key -> comment -> key;
   - rejection неизвестной версии и malformed input;
   - ограничение длины;
   - стабильность digest test vector;
   - отсутствие зависимости от временной generation.
6. Добавить новые source files в корневой и test target CMake lists.

### Критерий приёмки

- `make test` проходит.
- Сгенерированные iptables/nftables commands byte-for-byte не изменились.
- Нет plugin registry, capability abstraction или snapshot model.

## PR 2. Построить FirewallPlan рядом с существующим apply

### Цель

Получить единый canonical desired plan, сохранив старый backend API через тонкий
adapter.

### Работа

1. Добавить `FirewallPlan` и `FirewallRuleRegistrar`.
2. Registrar:
   - проверяет непустые module/instance IDs;
   - отклоняет duplicate keys;
   - назначает `insertion_order`;
   - валидирует family/hook compatibility;
   - сортирует по `(stage, priority, insertion_order)`;
   - сохраняет исходный config order для route rules.
3. Вынести из `apply_runtime_firewall()` чистую фазу
   `build_firewall_plan(inputs)`.
4. Оставить list preparation и streaming снаружи plan там, где этого требует
   текущий apply-mode contract.
5. Добавить compatibility adapter:
   - `MarkAction` -> `create_mark_rule()`;
   - `VerdictAction::drop` -> `create_drop_rule()`;
   - `VerdictAction::pass` -> `create_pass_rule()`;
   - `BalanceAction` -> `create_balance_rule()`;
   - plan prefilter -> существующий `set_global_prefilter()`.
6. `apply_runtime_firewall()` должен выполнять последовательность:
   - prepare inputs;
   - build plan полностью;
   - validate plan и backend requirements;
   - `firewall.prepare_apply(mode)`;
   - declare/load permitted sets;
   - replay plan через adapter;
   - `firewall.apply(mode)`;
   - вернуть `FirewallState` projection только после success.
7. Ошибка build/validation должна возникать до `prepare_apply()` и любых команд.

### Тесты

- Snapshot/structural equality старого recorded-call sequence и adapter output.
- Duplicate key, invalid mark/mask, invalid output/prerouting combination.
- Stable ordering при одинаковых priority.
- Ошибка balance на iptables возникает до mutation fake backend.
- Все apply modes дают тот же set/rule behavior, что в PR 0.

### Критерий приёмки

- Kernel-facing output не изменён.
- `apply_runtime_firewall()` больше не смешивает semantic construction с
  последовательностью `create_*` вызовов.
- Старые backend virtual methods пока сохранены.

## PR 3. Перенести backend compatibility в plan validation

### Цель

Удалить из runtime специальные проверки `default_gateway/balance requires
nftables`, не строя преждевременно большой capability framework.

### Работа

1. Ввести минимальный `FirewallBackendSupport` (`iptables`, `nftables`, `both`)
   либо функцию `supports(rule, backend)` рядом с canonical actions.
2. Закодировать только реально существующие ограничения:
   - `BalanceAction` — nftables;
   - current default-gateway semantics — nftables;
   - прочие существующие actions — согласно фактической поддержке.
3. Валидировать весь plan до backend mutation и сообщать:
   - module ID;
   - instance ID;
   - выбранный backend;
   - неподдерживаемую capability/semantic feature.
4. Удалить дублирующие `needs_nftables` branches из runtime.
5. Не вводить `uint64_t Capability` до появления третьего независимого
   ограничения, которое нельзя выразить action/criteria inspection.

### Тесты

- Nft-only plan проходит на nftables и отклоняется на iptables.
- Смешанный plan отклоняется целиком до первого backend call.
- Error text стабилен и пригоден для API/log diagnostics.

### Критерий приёмки

- Центральный runtime не знает названия balance/default-gateway feature.
- Нет частично подготовленного backend state после validation failure.

## PR 4. Добавить ownership comments в emitted rules

### Цель

Связать canonical identity с физическими rules, не меняя matching semantics.

### Предварительный preflight

1. Подтвердить доступность xt_comment в целевых Keenetic/OpenWrt artifacts.
2. Проверить фактические лимиты comment для используемых iptables/nft versions.
3. Убедиться, что `iptables-save`, `iptables -S` и `nft -j` возвращают comment в
   формах, которые можно надёжно parse.
4. Если iptables comment module отсутствует на поддерживаемой платформе, не
   делать comment обязательным для apply: оставить canonical positional
   matching и зафиксировать platform limitation. Не загружать kernel module из
   daemon автоматически.

### Работа

1. Расширить internal pending rule обоих backend'ов `FirewallRuleKey`.
2. Adapter передаёт key вместе с action/criteria.
3. Iptables emitter добавляет `-m comment --comment ...` в однозначном месте до
   terminal target.
4. Nftables emitter добавляет native rule comment в JSON batch.
5. Escaping выполняется существующими argument/JSON mechanisms; shell string
   concatenation для пользовательских данных не добавляется.
6. Старые verifier parsers временно учатся игнорировать/сохранять comment так,
   чтобы health не регрессировал.
7. Reconciler ordered representation нормализует comment единообразно.

### Тесты

- Точный iptables output для IPv4/IPv6, RAW/mangle и OUTPUT.
- Точный nft JSON shape.
- Quotes/backslashes и maximum-length key.
- Comment присутствует на каждом owned policy rule и не меняет match/action.
- Rules с правильным comment, но неправильным mark не считаются корректными.

### Integration

```sh
rtk make integration-tests-iptables
rtk make integration-tests-nftables
```

Проверить apply, restart, `RulesOnly`, `PreserveSets`, A/B switch, health и
cleanup. Никакие команды не запускать на реальном router без отдельного запроса.

### Критерий приёмки

- Все новые rules имеют stable ownership ID там, где platform поддерживает его.
- Transaction count не увеличился.
- Apply и cleanup сохраняют прежнюю область ownership.

## PR 5. Разделить backend inspection и semantic verification

### Цель

Один раз прочитать kernel firewall и получить backend-neutral snapshot.

### Модель

```cpp
struct ObservedFirewallRule {
    std::optional<FirewallRuleKey> key;
    FirewallHook hook;
    FirewallFamily family;
    FirewallRuleCriteria criteria;
    FirewallRuleAction action;
    std::string raw;
};

struct FirewallSnapshot {
    bool available{false};
    std::vector<ObservedFirewallRule> rules;
    std::vector<ObservedFirewallSet> sets;
    std::vector<ObservedFirewallChain> chains;
};
```

### Работа

1. Сначала вынести parsing без изменения public `FirewallVerifier`:
   - iptables text/ipset output -> snapshot;
   - nft JSON -> snapshot.
2. Сохранить backend-specific normalization внутри inspectors:
   - iptables family из команды/table context, не из set naming convention;
   - explicit TCP/UDP expansion;
   - address/CIDR normalization;
   - port normalization;
   - mark value/mask;
   - nft expression normalization;
   - hook и OUTPUT/PREROUTING.
3. Inspector выполняет фиксированное число command runner вызовов на health
   check, не по одному на rule.
4. Добавить общий `verify_firewall_plan(plan, snapshot)`:
   - найти по key;
   - zero -> missing;
   - больше одного -> duplicate;
   - один, но semantics различаются -> mismatch с field-level detail;
   - совпадение -> ok;
   - owned observed key отсутствует в desired plan -> extra.
5. Для legacy rules без comment временно оставить старый semantic fallback,
   ограниченный текущим active generation/owned chains.
6. Не удалять старые verifier entry points до перевода health checker.

### Тесты

- Одинаковые canonical fixtures для iptables и nftables inspectors.
- Missing, duplicate, mismatch и extra.
- Правильный key + неправильный action/criteria.
- Неправильный key + совпадающая semantics.
- Неизвестный `kpbr:v2` comment.
- Foreign rules/comments не объявляются owned extras.
- Несколько expected rules с одинаковой semantics, но разными keys.
- Command runner invocation count не зависит от числа expected rules.

### Критерий приёмки

- Backend parsers не получают `RuleState` для построения expectations.
- Общий verifier не parse'ит iptables text или nft JSON.
- Snapshot создаётся один раз за проверку.

## PR 6. Перевести RoutingHealthChecker на active FirewallPlan

### Цель

Health сравнивает фактический snapshot с последним успешно применённым plan.

### Работа

1. Добавить active `FirewallPlan` в `FirewallState` либо рядом с ним в
   `RuntimeStateSnapshot`.
2. Не дублировать mutable copies. Plan публикуется вместе с остальным runtime
   snapshot под существующей synchronization boundary.
3. `RuleState` строить как projection plan для API/control compatibility:
   - `rule_index` из source/config metadata;
   - list/set names из resource references;
   - outbound tag из diagnostics metadata;
   - action/fwmark/criteria из canonical action.
4. `RoutingHealthChecker`:
   - получает active plan;
   - вызывает backend inspector один раз;
   - запускает общий verifier;
   - сохраняет текущую форму API report либо делает additive extension через
     schema/codegen в отдельном PR.
5. При отсутствии active plan во время startup вернуть явный not-ready/unknown,
   а не сравнивать kernel с пустым desired state и объявлять все rules extra.
6. При failed apply health продолжает проверять предыдущий active plan; ошибка
   candidate apply отражается существующим runtime-state механизмом.
7. После перехода удалить factory backend-specific semantic verifier из health
   path. Factory inspector может остаться.

### Тесты

- Успешный apply атомарно публикует plan и projection.
- Failed apply не заменяет active plan.
- Startup без plan.
- Concurrent snapshot reader не видит смесь old/new state.
- Health missing/mismatch/extra/duplicate для обоих backend fixtures.
- Control/API projection не меняется неожиданно.

### Проверка

```sh
rtk make test
rtk make clang-check
```

### Критерий приёмки

- Health semantics больше не реконструируется из `RuleState`.
- `RuleState` не является source of truth.
- Нет отдельной backend-specific expected-rule expansion в health path.

## PR 7. Выделить первые rule modules: route mark/drop/pass

### Цель

Доказать module boundary на уже существующем и хорошо покрытом route loop.

### Работа

1. Добавить общий `FirewallBuildContext` только с реально нужными immutable
   ссылками/views:
   - route rules;
   - resolved rule state/marks;
   - prepared set references;
   - selected backend или backend support descriptor;
   - balance candidates позднее.
2. Не превращать context в service locator и не передавать в него `Firewall`.
3. Создать:
   - `RouteMarkRuleModule`;
   - `RouteDropRuleModule`;
   - `RoutePassRuleModule`.
4. Вынести из runtime только решение `condition -> RuleInstance`.
5. Общую expansion criteria/targets вынести в одну существующую или минимальную
   pure helper, используемую всеми тремя modules; не копировать loops.
6. Явный registry manifest возвращает modules в детерминированном порядке.
7. Runtime продолжает готовить config/lists/marks и вызывает registry build.

### Identity

Instance ID должен включать только поля, различающие физические canonical rules:

- стабильный config rule identity/index;
- expanded list/set target;
- family;
- protocol expansion;
- hook;
- action variant при необходимости.

Не включать текущий mark value, если изменение mark должно считаться mutation той
же logical rule, а не remove/add новой identity.

### Тесты

- Каждый module возвращает zero/one/many expected instances.
- Одинаковые inputs дают одинаковые keys и order.
- Mark value change сохраняет key и даёт semantic mismatch.
- Reordering config rules предсказуемо меняет plan order.
- Runtime recorded-call fixtures PR 0 остаются эквивалентными.

### Критерий приёмки

- Выбор mark/drop/pass отсутствует в центральном route apply loop.
- Добавление ещё одного route action не требует нового branch в runtime.
- Backend files не знают имён modules.

## PR 8. Выделить route balance

### Цель

Проверить архитектуру на nft-only action и conntrack/owned-mark semantics.

### Работа

1. Добавить `RouteBalanceRuleModule` в явный manifest.
2. Module формирует `BalanceAction` без nft JSON.
3. Plan validation отклоняет action на iptables до apply.
4. Nft compiler остаётся единственным владельцем:
   - random selection expression;
   - setter chains;
   - owned mark mask;
   - conntrack mark interaction;
   - fallback mark.
5. Setter chains считать backend internal resources, а не публичными policy
   modules.
6. Проверить, что изменение candidates приводит к ожидаемому plan/semantic hash
   и `RulesOnly` обновляет rules без mutation sets.

### Тесты

- Zero/one/many candidates и fallback.
- Candidate order/deduplication согласно текущей семантике.
- Mark mask применяется в широком integer type без преждевременного overflow.
- Iptables rejection до command calls.
- Nft batch остаётся единым.
- Connmark restore/owned bits не повреждают чужие bits.

### Integration

- Nftables load-balancing case в QEMU.
- Restart и health после изменения selection/candidates.
- Partial command failure не публикует новый plan.

### Критерий приёмки

- Для nft-only policy не меняется базовый `Firewall` interface.
- Runtime не содержит balance-specific backend check.

## PR 9. Выделить DNS detour

### Цель

Удалить отдельную post-route генерацию DNS rules из runtime.

### Работа

1. Создать `DnsDetourRuleModule`.
2. Context предоставляет уже валидированные DNS endpoints и mark target.
3. Module формирует OUTPUT rules для TCP/UDP и нужных families.
4. Identity различает endpoint, family и protocol, но не зависит от input order,
   если endpoints семантически являются set.
5. Backend использует обычный mark action compiler; DNS-specific code в backend
   не добавляется.

### Тесты

- IPv4/IPv6 endpoint.
- TCP и UDP order.
- Duplicate endpoints.
- Invalid/absent endpoint создаёт zero rules согласно текущей validation model.
- Health обнаруживает missing только для конкретного DNS instance.

### Критерий приёмки

- Runtime не содержит отдельного DNS firewall loop.
- Modules используют уже существующие criteria/action primitives.

## PR 10. Мигрировать global prefilter без потери backend semantics

### Цель

Разделить `FirewallGlobalPrefilter` на policy modules после стабилизации обычных
canonical rules.

### Почему поздно

Prefilter затрагивает chain placement, conntrack state, RAW/mangle differences и
ранний return. Его нельзя переносить раньше characterization и snapshot слоёв.

### Работа

1. Зафиксировать backend-specific semantics отдельных операций:
   - restore owned ctmark bits;
   - skip established/DNAT;
   - skip already marked packets;
   - inbound interface restriction.
2. Решить для каждой операции, выражается ли она текущими action/criteria:
   - если да — обычный `FirewallRuleInstance`;
   - если нет — добавить ровно один необходимый action/matcher primitive.
3. Создать modules:
   - `RestoreConntrackMarkRuleModule`;
   - `SkipEstablishedOrDnatRuleModule`;
   - `SkipMarkedPacketsRuleModule`;
   - `InboundInterfaceFilterRuleModule`.
4. Назначить stages до route/DNS classification согласно текущему реальному
   порядку, а не предполагаемой схеме.
5. Временно сравнить generated backend output с PR 0 fixtures.
6. После полной parity удалить compatibility `global_prefilter` payload и
   `set_global_prefilter()`.

### Тесты

- RAW и mangle modes iptables отдельно.
- IPv4/IPv6 placement.
- Connmark mask сохраняет не-owned bits.
- Established/DNAT order относительно restore и skip-marked.
- Inbound interface positive/negative/empty cases.
- Nft expression order и chain placement.
- Health canonicalization каждой операции.

### Integration

Оба backend QEMU suites обязательны, включая существующие routing/firewall
case'ы. Компиляции недостаточно.

### Критерий приёмки

- `FirewallGlobalPrefilter` и `set_global_prefilter()` удалены.
- Порядок prefilter rules явно виден в plan.
- Ни один module не знает physical chain/generation name.

## PR 11. Перевести backend API с create_* на apply(plan)

### Цель

После миграции policies удалить compatibility adapter и сделать plan настоящим
backend input.

### Работа

1. Добавить/оставить единый backend entry point:

   ```cpp
   virtual void apply(const FirewallPlan&, FirewallApplyMode) = 0;
   ```

2. Iptables compiler проходит plan и строит существующие restore fragments в
   памяти; generation manager и set manager остаются внутри backend.
3. Nftables compiler проходит plan и добавляет commands в существующий JSON
   batch; transaction submit остаётся один.
4. Удалить публичные `create_mark_rule`, `create_drop_rule`, `create_pass_rule`,
   `create_balance_rule` после удаления всех callers.
5. `prepare_apply()` объединять с `apply(plan, mode)` только если нет внешнего
   caller, которому реально нужна отдельная подготовка. Не вводить новый
   transaction interface без необходимости.
6. Pending rule variants можно заменить canonical action visitor только после
   подтверждения, что это уменьшает код и не смешивает generation state с plan.
7. Sets оставить backend lifecycle concern; plan содержит desired declarations
   и references, backend выбирает безопасный порядок их подготовки.

### Failure flow

Для каждого backend тестом закрепить:

1. plan полностью валиден;
2. inactive transaction/generation подготовлена;
3. sets обработаны согласно mode;
4. rules compiled;
5. backend commit выполнен;
6. live state проверен;
7. только затем caller публикует active plan;
8. при любой ошибке pending buffers очищены и не протекают в следующий apply.

### Критерий приёмки

- Firewall public API не перечисляет feature actions.
- Backend принимает упорядоченный desired plan.
- A/B generation, nft batch и apply modes сохраняются.

## PR 12. Удалить legacy verifier duplication

### Цель

Завершить переход после достаточного периода parallel verification.

### Работа

1. На один переходный релиз при возможности запускать old и canonical verifier
   на тестовых fixtures/diagnostic mode и сравнивать результаты, не удваивая
   production command execution.
2. Удалить:
   - backend-specific expected `RuleState` expansion;
   - family inference из set naming там, где snapshot уже знает family;
   - duplicate criteria equality;
   - semantic verifier factory из health path;
   - legacy no-comment fallback после окончания compatibility window.
3. Оставить backend-specific parsers/inspectors и общий canonical comparator.
4. Обновить diagnostics так, чтобы mismatch содержал key и отличающиеся поля,
   но не печатал чувствительные или чрезмерные raw payloads.
5. Удалить мёртвые adapters, tests и includes; проверить source lists CMake.

### Критерий приёмки

- Одна реализация expected semantics: `FirewallPlan`.
- Одна canonical equality implementation.
- Backend-specific код ограничен compile, inspect и lifecycle.
- Общий объём удалённого legacy-кода не меньше добавленной замены без учёта
  тестов; если это не так, остановиться и проверить лишние abstractions.

## PR 13. Опционально разделить крупные backend-файлы

Этот этап выполнять только если после PR 12 файлы всё ещё мешают независимому
изменению и тестированию. Само уменьшение числа строк не является целью.

Возможные естественные границы:

- iptables compiler;
- iptables set manager;
- iptables generation manager;
- iptables inspector;
- nftables compiler;
- nftables set manager;
- nftables inspector.

Правило выделения: новый компонент должен иметь минимум два реальных caller/use
cases либо заметно изолировать transaction/failure boundary. Интерфейс с одной
реализацией не добавлять; достаточно concrete helper class или namespace
functions.

## 5. Детальный порядок modules

Registry создаётся явным manifest в фиксированном порядке:

```text
restore_conntrack_mark
skip_established_or_dnat
skip_marked_packets
inbound_interface_filter
dns_detour
route_drop
route_pass
route_mark
route_balance
```

Manifest order не заменяет stages. Реальный итоговый порядок определяется plan
sort и должен повторять текущее backend behavior. Если characterization tests
показывают другой порядок, stage/priority назначаются по тестам, а не по этому
примеру.

Для route modules следует избегать тройного прохода по всем правилам, если это
меняет порядок. Минимальный вариант — один route policy module, внутри которого
action-dispatch делегирует трём small pure builders. Разделять его на три
registry modules стоит только если можно сохранить config order без merge
сложности. Acceptance criterion важнее буквального количества классов.

## 6. Capability evolution

Начать с проверки конкретных canonical constructs. Полный capability set вводить
только при фактической необходимости.

Возможная последовательность:

1. `supports(FirewallRuleAction, FirewallBackend)`;
2. проверка `criteria.default_gateway`;
3. после третьего независимого случая — enum capabilities:
   - named sets;
   - dynamic set timeout;
   - conntrack mark;
   - output classification;
   - raw prerouting;
   - random balance;
   - default gateway classification.

Capabilities описывают backend/runtime platform, а не только имя backend. Probe
kernel features выполняется до apply и не смешивается с module registration.

## 7. Identity и migration compatibility

### 7.1. Формирование instance ID

Canonical serialization должна:

- использовать фиксированный порядок полей;
- различать family, hook и protocol expansion;
- нормализовать CIDR/ports до hash;
- сортировать только те collections, чья semantics действительно unordered;
- сохранять config order там, где он влияет на first-match behavior;
- не включать временные names/handles/generation;
- не использовать локализованный display text.

### 7.2. Изменение semantics

При изменении mark/action/criteria logical key обычно остаётся тем же. Тогда
verifier сообщает mismatch, а apply заменяет rule. Версию key schema менять
только если меняется определение logical identity.

### 7.3. Upgrade со старой версии

- Первый apply после обновления может увидеть legacy rules без comments.
- Inspector ограничивает fallback текущими owned chains/generations.
- Destructive/PreserveSets apply заменяет их rules с IDs.
- Health во время окна миграции различает `legacy-unidentified` и foreign rule.
- Cleanup не удаляет rule только по подозрительно похожему comment.

## 8. Verification matrix

Минимальная матрица для каждого затронутого этапа:

| Область | iptables | nftables |
|---|---:|---:|
| IPv4 | обязательно | обязательно |
| IPv6 | обязательно | обязательно |
| PREROUTING | обязательно | обязательно |
| OUTPUT | обязательно | обязательно |
| mark/drop/pass | обязательно | обязательно |
| balance | reject до mutation | обязательно |
| default gateway | reject/текущий контракт | обязательно |
| static sets | обязательно | обязательно |
| dynamic sets | обязательно | обязательно |
| `Destructive` | обязательно | обязательно |
| `PreserveSets` | обязательно | обязательно |
| `StaticSetsOnly` | обязательно | обязательно |
| `RulesOnly` | обязательно | обязательно |
| health missing/mismatch/extra | обязательно | обязательно |
| restart/reconcile | integration | integration |
| partial failure | fixture/integration | fixture/integration |

Focused unit checks запускаются на каждом PR. Полный baseline:

```sh
rtk make test
```

После изменений transaction, inspector, prefilter, ordering или apply modes:

```sh
rtk make clang-check
rtk make integration-tests-iptables
rtk make integration-tests-nftables
```

Cross-build выполняется перед релизом, если изменены platform-facing firewall
commands или использован новый kernel/userspace feature.

## 9. Review checklist для каждого PR

### Architecture

- Module описывает why/what, но не how.
- Backend не содержит config-specific policy branch.
- Runtime не содержит backend syntax/capability special case.
- Lifecycle не просочился в module.
- Не создана abstraction только ради будущего использования.

### Ordering

- Stage и priority явны.
- Config order сохранён.
- Expansion order протестирован.
- Нет зависимости от unordered containers или static initialization.

### State и failure

- Candidate plan не публикуется до success.
- Старый active plan сохраняется после failure.
- Pending backend buffers очищаются.
- Retry/fallback выполняется не более предусмотренного числа раз.
- Health проверяет active, а не последний attempted plan.

### Firewall correctness

- Fwmark/connmark masks сохраняют чужие bits.
- IPv4/IPv6 рассмотрены отдельно.
- RAW/mangle placement не предполагается одинаковым.
- Owned/foreign state различается.
- Sets и rules применяются в безопасном порядке.
- Nft batch остаётся атомарным; iptables dispatcher switch остаётся последним
  publication step.

### Tests

- Есть один минимальный runnable check на новую non-trivial logic.
- Тест проверяет observable behavior, а не private implementation detail, кроме
  точного backend compiler output fixtures.
- Нет реальных firewall/network mutations в unit tests.
- Environmental limitations записаны явно.

## 10. Метрики завершения

Миграция считается завершённой, когда:

1. `apply_runtime_firewall()` готовит inputs, строит plan и передаёт его backend,
   но не выбирает конкретный action/backend syntax.
2. `Firewall` public API не требует нового virtual method при добавлении policy,
   использующей существующие match/action primitives.
3. Health получает один snapshot и сравнивает его с active plan.
4. `RuleState` является projection, а не source of truth.
5. В backend verifier нет независимой реконструкции expected rules из config.
6. Новая generic policy требует module + tests + manifest entry; backend меняется
   только при появлении нового primitive.
7. Nft-only policy объявляет ограничение рядом с canonical construct/module и
   отклоняется до mutation.
8. Apply modes, nft transaction и iptables A/B behavior проходят integration
   matrix.
9. Extra owned rules обнаруживаются, foreign rules игнорируются.
10. После удаления legacy пути итоговая production complexity уменьшается.

## 11. Рекомендуемая первая итерация

Практический первый milestone — PR 0–3. Он даёт canonical plan и переносит
compatibility validation из runtime, но ещё не трогает kernel output, comments,
health и backend lifecycle. Это самая безопасная точка для архитектурной оценки.

Второй milestone — PR 4–6: ownership comments, snapshot и active-plan health.
Он даёт основной эксплуатационный выигрыш и требует integration проверки обоих
backend'ов.

Третий milestone — PR 7–12: постепенный перенос policies и удаление legacy API.
После каждого module migration сохраняется рабочий релизный state.

PR 13 не является частью definition of done и выполняется только при измеримой
пользе.
