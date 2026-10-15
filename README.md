# ascent-ios — мод-меню + Box ESP для Standoff 2 (iOS, arm64)

Что внутри:
- **мод-меню ПОВЕРХ игры** — плавающий квадратик + таскаемая панель с
  тумблерами. Живёт в dylib, поэтому висит над Standoff 2, а не только внутри
  лоадера (обычное приложение iOS так не умеет — его замораживают при уходе);
- **box ESP** (player pattern → world-to-screen → боксы) — то, что ты просил
  изначально.

Ставится **инъекцией в Standoff 2 через TrollFools**. Лоадер (.tipa) для
оверлея не нужен.

Ссылки на источники, которые я разобрал:

| Что | Где |
|---|---|
| Оффсеты | `C:\Users\akaov\Desktop\ыыыы телефон ыыыы ножик.hpp` (== `сурсы\wonderful\offsets.hpp`) |
| IL2CPP дамп | `C:\Users\akaov\Desktop\иоз чич\гг окей екр гоу\` (`dump.cs`, `script.json`) |
| Рабочий референс (Windows) | `C:\Users\akaov\Desktop\сурсы\wonderful\` |

---

## Что такое "player pattern"

Это 22-байтовая сигнатура трансформ-блока игрока (TMatrix). Из
`wonderful/cheat.cpp:17`:

```
00 00 80 3F  00 00 80 3F  00 00 80 3F    -> scale.xyz = 1,1,1
80 96 18 4B                              -> scale.w   = 1e7
00 00 00 00  CD CC
```

Блок TMatrix: `rotation[0x00]`, `position[0x10]`, `scale[0x20]` (по 4 float).
Паттерн ловит поле `scale`, значит **база блока = найденный адрес − 0x20**, а
мировая позиция игрока — `block + 0x10`.

Камера: маркер `mtx_checker` (6 float, `a2` = FOV 10..170), а сама
view-projection матрица лежит по `marker − 0x78`.

---

## Архитектура

```
src/
  math.hpp     Vec3 / TMatrix / MtxChecker / Mat4 (WorldToScreen, looksLikeCamera)
  memory.hpp   перечисление writable-регионов + crash-safe чтение (vm_read_overwrite)
  game.hpp     Snapshot (позиции игроков + камера) — отдаётся оверлею
  game.cpp     СКАНЕР: playerpattern + mtx_checker, выбор камеры, фоновой поток
  overlay.mm   прозрачный UIWindow + CADisplayLink, рисует боксы CoreGraphics
  main.mm      constructor dylib'а: старт сканера и оверлея
```

Почему так (а не как в wonderful):

- **Мы внутри процесса Standoff 2**, поэтому не нужен `ReadProcessMemory` и
  трансляция guest→host из эмулятора. Читаем свой адресный спайс через
  `vm_read_overwrite` (не крашится на гонке с `munmap`).
- **Оверлей — обычный прозрачный UIWindow**, а не ImGui+Metal хук. Для коробок
  (и hp/ника позже) хватает CoreGraphics и это на порядок проще/стабильнее.
- Позиции блоков живут по стабильным адресам, поэтому полный скан раз в ~4 c,
  а между сканами перечитываем только позиции (33 мс).

## Как считается коробка (как в wonderful)

```
feet = w2s(position)
head = w2s(position + (0, 1.8, 0))
H    = |head.y - feet.y|
W    = H / 4
rect = (feet.x - W, head.y, 2W, H)
```

---

## Сборка

Вариант 1 — macOS + Theos:

```
make clean && make
# результат: .theos/obj/ascent.dylib
```

Вариант 2 — облако: пуш в репозиторий, GitHub Actions соберёт артефакт
`.github/workflows/build.yml`.

## Инжект в игру

1. Скачай `Standoff 2` и `TrollFools` (TrollStore).
2. В TrollFools выбери Standoff 2 → Inject → `ascent.dylib`.
3. Запусти игру, зайди в матч. Сверху будет кнопка **ASCENT** (переключатель ESP).

Лог: `<Documents игры>/ascent_log.txt` (или `%HOME%/Documents`).

---

## Контракт с лоадером (.tipa / Ascent)

Лоадер (то, что делает другой ИИ) должен **загрузить этот dylib в процесс игры**.
На iOS без джейлбрейка это делается одним из двух способов:

- **TrollFools** (рекомендуется): dylib вшивается в бандл Standoff 2 заранее.
  Лоадер в этом случае — это оболочка/брендинг + кнопка Start.
- **Инжектор из лоадера**: если у .tipa есть entitlement `task_for_pid` /
  `com.apple.private.cs.debugger` (TrollStore их выдаёт), лоадер по Start
  аттачится к процессу игры и подгружает dylib (как отладчик). Тогда путь к
  dylib лоадер передаёт сам.

Чтобы меню из лоадера управляло ESP, договоримся о формате — сейчас тумблеры
зашиты в `overlay.mm` (`EspConfig`). Скажи, и я добавлю чтение конфига
(например `/var/mobile/Library/Application Support/Ascent/config.json`) с
полями `enabled/boxes/filled/distance/status`, который лоадер будет писать
перед запуском.

---

## Ограничения v1 (честно)

- **Оффсеты от Android-сборки** (`libunity.so`/`kApkSha256`). Для iOS это ок,
  потому что мы НЕ используем нативные RVA — ищем по паттерну, а field-оффсеты
  (`ake::kStats` и т.п.) совпадают на arm64 (проверено по дампу: `CharacterBindings`
  `_bones 0x20`, `Tool 0x28`, `Animator 0x30`, `CharacterRenderer 0x38`,
  `ArmsRenderer 0x40`, `GlovesRenderer 0x48` — точь-в-точь как в offsets.hpp).
- Пока **нет team/hp/ника** — коробки рисуются на всех найденных TMatrix-блоках
  (возможно лишние, если игра имеет не-игровые трансформы с теми же scale).
  Команда/HP читаются через il2cpp-граф (`ake::kStats=0x70`, `ylw::kHp=0x10`,
  `kTeam=0x90`) — это следующий шаг (см. `gamedata.cpp` в wonderful).
- Не проверено на живом устройстве: я не могу собрать iOS отсюда. Первый запуск
  может потребовать правки дельт (`kBlockDelta`, `kMatrixDelta`) под точную
  версию игры.

## Что дальше (по приоритету)

1. Собрать dylib, заинжектить, посмотреть `ascent_log.txt` + статус-строку
   (`players=.. camera=..`).
2. Если `camera=no` — уже добавлен оконный fallback (поиск матрицы вокруг
   маркера, `resolveCamerasFromMarkers`); если и он не помогает — прислать лог,
   проверю дельты под точную версию игры.
3. Команда/HP/имя через il2cpp API (или через world→objects, как в wonderful).
4. Скелет по `CharacterBindings._bones`.
