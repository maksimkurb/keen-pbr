# Keen PBR: оставшиеся шаги упрощения firewall-архитектуры

> План отражает состояние на 1 октября 2026 года. Основные описанные в нём
> переносы и упрощения superseded актуальной реализацией; этот документ
> сохранён как история решений. Текущая архитектура описана в
> [firewall-architecture.md](firewall-architecture.md).

Baseline: ветка `feature/load-balancing`, commit `4713d733`.
Обновлено: 1 октября 2026 года. Это план, не отчёт о выполнении.

Этот документ заменяет порядок следующих задач из обсуждения. Исторический
[план миграции](firewall-rule-modules-migration-plan.md) сохраняется как контекст,
но перенос semantic verification в каждый policy module больше не является целью.

## Текущее состояние и проблема

Уже введены canonical rules с keys/stages/criteria/actions, manifest девяти
модулей и общий snapshot verifier. Legacy `create_*` replay API,
`firewall_reconciler` и `runtime_reconciler` удалены. Повторять эту миграцию не нужно.

По предоставленному ревью серии из 47 commits / 392 file diffs:

- `src/firewall`: net +3 480 строк; C++ tests: net +3 558 строк;
- основная сложность сосредоточена в `firewall_plan_verifier.cpp`, а не в девяти модулях;
- PR смешивает balance/default_gateway/UI, housekeeping, refactor и новый netns harness.

Эти числа — baseline из ревью, не новая независимая оценка. Ревью основано на
subjects/stat/key headers, не на построчном чтении всех изменений. Увеличение LOC
частично объясняется новыми функциями, ownership comments и capability probing;
само по себе оно не доказывает ни успех, ни провал архитектуры.

Цель: новый policy на существующих primitives добавляется через свой модуль,
manifest и тесты, без изменений runtime/backend/health semantics. Модули задают
WHY/WHAT; общий lowering, compiler и inspector задают HOW. Проверка не должна
повторно интерпретировать policy.

## Этап 0. Подготовить PR к проверяемому ревью

### Работа

- Разложить commits и зависимости минимум на feature PR, refactor PR и harness PR.
  Housekeeping вынести отдельно либо явно обосновать его принадлежность.
- Если изменения зависимы, использовать последовательность зависимых PR;
  не делать вид, что каждый из них независим. Каждый должен собираться и иметь
  собственные проверки. Переписывание опубликованной истории требует отдельного согласования.
- Заполнить description/checklist: scope, инварианты, результаты тестов,
  ограничения окружения и порядок ревью.
- Найти точный первый balance commit и записать число затронутых файлов:
  production, tests, build/docs/generated отдельно. Проверить, охватывает ли
  один commit всю feature; иначе дополнительно измерить её полный диапазон.
- Отдельно измерять refactor, feature и harness, чтобы LOC новых возможностей
  не выдавать за стоимость архитектурной миграции.

### Приёмка

- Есть карта commits → PR и явные зависимости.
- Baseline содержит commit/range, способ подсчёта и результаты, а не оценку на глаз.
- Число затронутых файлов сравним с **следующим реальным policy** позднее.
  Придумывать feature ради прохождения этого критерия не нужно.

## Этап 1. Отделить desired plan от apply result — первая задача разработки

### Работа

- Убрать `applied_physical_set_names` из `FirewallPlan`: active A/B generation
  и физические имена — результат применения, не desired intent.
- Сделать `apply()` возвращающим результат реализации плана. Сохранить отдельную
  preparation/staging фазу там, где она нужна для имён sets до streaming lists.
- Публиковать `{plan, apply_result}` одним immutable active object только после
  успешного применения; `RuleState`/API projection получать из этого состояния.
  Не создавать отдельный status service или новые параллельные копии истины.
- `RulesOnly` сверяет текущую generation с предыдущим **apply result**.
  Сохранить ровно один fallback в `PreserveSets`.
- Явно определить границу успеха: подготовка, kernel publication, runtime/API
  publication. Ошибка до commit сохраняет прежний active object. Если kernel уже
  изменён, а последующая операция упала, сообщать partial failure/drift;
  не обещать rollback, которого backend не обеспечивает.

### Тесты и приёмка

- Desired plan не меняется из-за физической generation после apply.
- Успех публикует согласованную пару plan/result; injected preparation/apply
  failure не публикует новую пару.
- Проверены смена A/B generation, `RulesOnly` preflight/fallback и ошибка после
  kernel publication. Существующие failure-injection тесты переиспользованы.
- API/health читают одно active состояние, не смесь старого и нового apply.

## Этап 2. Завершить общий lowering и сократить verifier

### Работа

- Развить существующий `materialize_firewall_classifiers()`, а не создавать
  конкурирующий framework: logical rule → ordered physical rules.
- Использовать этот lowering в обоих backend compilers и для expected snapshot.
  Backend-specific expansion допустим, но должен определяться в одном месте.
- Включить недостающие expansion для prefilters, family/hook/protocol/interface,
  companion rules и balance helpers; сохранить явный порядок и ownership.
- Inspector парсит фактический ruleset и нормализует представление. Verifier
  сравнивает expected/observed структуры, multiplicity и порядок без знания
  `route.mark`, `route.balance` и других policy.
- Пересмотреть `balance_rule_mismatch_detail()` и commit о переносе проверки
  balance в модуль. Selector/modulus/vmap/setter/connmark нужно проверять, но
  место проверки определяется normalized physical semantics, не названием policy.
  Custom normalization оставить только при доказанном различии kernel representation,
  с примером actual output и тестом. Отдельный `check()` для каждого policy не добавлять.
- Сохранить ограниченный fallback для environments без ownership comments:
  только active owned scope, со сравнением semantics. Comment сам по себе не доказательство.

### Тесты и приёмка

- Один logical intent имеет один shared expected expansion; backend и verifier
  не содержат независимых списков expected policy rules.
- Есть independent parser fixtures из реального iptables-save/nft JSON и
  packet tests: использование общего lowering в тесте не доказывает его правильность.
- Corruption checks обнаруживают неверные match/action/mask, отсутствие,
  duplicate, unexpected owned rule и нарушенный порядок.
- Balance helpers проверяются полностью; отсутствие comments не отключает проверку.
- `firewall_plan_verifier.cpp` становится проще за счёт удаления policy expansion,
  а не переноса того же дублирования по девяти файлам. Зафиксировать diff/LOC,
  но не считать произвольный лимит размера самостоятельным критерием качества.

## Этап 3. Один источник условий prefilter и один manifest

### Работа

- Co-locate enable conditions с соответствующим prefilter policy. Conntrack
  setup и построение firewall используют одно решение, а не вычисляют его дважды.
- Убрать повторение `std::array<..., 9>` в header/source. Дедуцировать размер
  manifest в одном месте и предоставить обход без ручного count. Сохранить C++17;
  `std::span` или новая dependency для этого не нужны.
- Сохранить explicit deterministic manifest и config order.
- Замена stateless classes функциями факультативна: делать только если реально
  удаляет wrappers/boilerplate. Сама по себе это не архитектурная задача.

### Тесты и приёмка

- Restore/conntrack/prefilter conditions проверены для обоих backend и отсутствующих marks.
- Добавление module entry не требует менять размер массива в другом файле.
- Runtime, backend и health не включают конкретные policy module headers.
  Общие model/context/manifest headers допустимы. Этот dependency guard —
  проверяемый сейчас proxy расширяемости, не обещание о будущей feature.

## Этап 4. Закрыть реальные packet и corruption gaps

Тесты пишутся **вместе с каждым этапом**, а не откладываются до этого этапа.
Здесь завершаются оставшиеся gaps после инвентаризации уже существующего покрытия.

### Работа

- Добавить dedicated netns acceptance для `restore_conntrack_mark`,
  `skip_established_or_dnat` и `inbound_interface_filter`.
- Проверять реальное направление пакетов/marks и negative controls, а не только
  текст generated rules. Для restore учитывать owned/unowned bits и направление
  соединения; для bypass — established/DNAT и новый поток; для interfaces —
  разрешённый/запрещённый ingress и соответствующую OUTPUT semantics.
- Для каждого класса verifier checks добавить netns corruption: применить
  корректный plan, повредить rule, проверить health degradation, восстановить
  состояние и проверить recovery. Выполнять на обоих backend там, где feature
  поддерживается; balance — nft-only, unsupported capability — negative test.
- Проверить relevant IPv4/IPv6, RAW/mangle placement, apply modes, generation
  switch и partial failure gaps. Не дублировать уже покрытые сценарии.
- Выводить capability preflight `OK/WARN/ERROR`: обязательная capability блокирует
  запуск до case, отсутствие необязательной явно обозначено. Не превращать
  незапущенный обязательный сценарий в PASS.
- Вывести imports/`CASE_MODULES`/expected registration names из одного manifest
  интеграционных cases. Независимые assertions поведения сохранить: registry
  consistency не заменяет correctness test.

### Приёмка

- У каждой поддерживаемой policy есть positive/negative packet evidence.
- У каждого verifier invariant есть соответствующая corruption проверка
  (несколько вариантов допустимо объединить в один case).
- Foreign rules не считаются owned и не удаляются. Неизвестные comments не
  расширяют ownership; fallback без comments протестирован отдельно.
- Отчёт различает PASS, capability/environment limitation и реальную ошибку.
  Тесты работают в изолированном harness и не меняют networking host/router.

## Условный этап. Разделить backend compiler и lifecycle

Только если реальное изменение вынуждает одновременно менять compilation и
transaction/lifecycle в `iptables.cpp` или `nftables.cpp`.

- Вынести минимальную связанную часть, переиспользуя общие physical model/lowering.
- A/B generations, dispatcher publication, sets preservation, batching и cleanup
  оставить backend-owned; policy ничего об этом не знает.
- Приёмка: конкретное изменение становится локальнее, проверено прежнее packet,
  apply-mode и failure поведение. Длина файла сама по себе не основание для разбиения.

## Порядок выполнения и проверки

1. Этап 0: review packaging и baseline.
2. Этап 1: desired/apply result boundary.
3. Этап 2: shared lowering/normalized diff.
4. Этап 3: conditions/manifest/dependency guard.
5. Этап 4: оставшиеся packet/corruption gaps.
6. Условный backend split — только при появлении основания.

Каждый этап — отдельно проверяемый PR с тестами этого изменения. Для C++ использовать
root Makefile: `rtk make`, `rtk make test`, затем relevant netns cases для iptables
и nftables через `make integration-tests-iptables` / `make integration-tests-nftables`
с `INTEGRATION_CASES`. Для concurrency-sensitive изменений дополнительно
`rtk make clang-check`. Docs-only изменения сборки не требуют.

## Итоговый критерий чистой архитектуры

- Desired intent не содержит результат предыдущего apply.
- Active plan/result публикуются согласованно; failures не скрывают рассогласование с kernel.
- Policy conditions и logical expansion имеют по одному источнику истины.
- Backend и health не знают конкретные policy modules; verifier не является вторым emitter.
- Реальное packet поведение и обнаружение corruption проверены, включая ограничения окружения.
- Следующая настоящая policy на существующих primitives измеряется относительно
  baseline этапа 0. Изменения compiler/inspector оправданы только новым primitive
  или доказанной normalization, не самим фактом добавления policy.
