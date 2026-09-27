# Управление вьюпортом и объектами

Задача: физическая песочница, в которой пользователь Blender, Houdini, Unity, Maya, 3ds Max или Unreal
чувствует себя дома с первой минуты, а новичок осваивает всё за 5 минут. Принцип: **как у всех, а где
у всех по-разному — как проще**. Всё ниже сверено с официальной документацией (ссылки в конце, `[B1]`
и т. п.). `·` в таблице значит «на прочитанных официальных страницах не описано или не применимо».

## 1. Карта действий

| Действие | Blender | Houdini | Unity | Maya | 3ds Max | Unreal | **Наш выбор** (почему) |
|---|---|---|---|---|---|---|---|
| Орбита | MMB [B1] | Space/Alt+LMB [H1] | Alt+LMB [U1] | Alt+LMB [M1] | Alt+MMB (Alt+LMB в Maya-режиме) [X1] | Alt+LMB; RMB = осмотр на месте [E1] | **ПКМ-драг**, а также **Alt+ЛКМ**. Alt+ЛКМ есть у 4 из 6; ПКМ — без модификатора |
| Панорама | Shift+MMB [B1] | Space/Alt+MMB [H1] | MMB [U1] | Alt+MMB [M1] | MMB [X1] | MMB, Alt+MMB [E1] | **СКМ-драг**, а также **Alt+СКМ** (оба варианта большинства) |
| Зум | колесо, Ctrl+MMB [B1] | колесо, Space/Alt+RMB [H1] | колесо, Alt+RMB [U1] | Alt+RMB [M1] | колесо, Ctrl+Alt+MMB [X2] | колесо, Alt+RMB [E1] | **колесо**, а также **Alt+ПКМ** |
| Зум к курсору | опция, по умолчанию выкл. [B15] | опция «к указателю» [H1] | · | · | от центра окна [X2] | · | **вкл. по умолчанию**: не нужно потом панорамировать [B15] |
| Показать выбранное | Numpad . (F в Industry) [B1][B9] | Space+G / Space+F [H1] | F [U1] | F [M4] | Z [X5] | F [E1] | **F** (у 5 из 6) |
| Показать всё | Home (A в Industry) [B1][B9] | Space+H, Shift+A [H1] | · | A [M4] | Ctrl+Alt+Z (все окна — Shift+Ctrl+Z) [X5] | · | **F без выделения** + Home: одна клавиша на «я потерялся» |
| Выбрать | LMB [B5] | LMB [H2] | LMB [U3] | LMB [M5] | LMB [X3] | LMB [E2] | **ЛКМ** (у всех) |
| Добавить к выделению | Shift+LMB (переключает) [B5] | Shift+LMB [H2] | Shift или Ctrl+LMB [U3] | Shift = переключить, Ctrl+Shift = добавить [M5] | Ctrl+LMB [X3] | Ctrl+LMB (переключает) [E2] | **Shift+ЛКМ и Ctrl+ЛКМ — оба переключают**: любая привычка работает |
| Снять выделение | Alt+A [B7] | Shift+N [H2] | · | Ctrl+LMB убирает [M5] | клик в пустоту, Alt+LMB убирает [X3] | · | **клик в пустоту**, Ctrl+Shift+A. Ctrl+A — выбрать всё (Unity [U12], 3ds Max [X5]) |
| Рамка | драг инструментом Select Box, B [B5] | драг инструментом Select [H2] | драг по пустому [U3] | · | драг по пустому [X3] | драг (в перспективе Ctrl+Alt+LMB) [E1][E2] | **ЛКМ-драг с пустого места**; Shift — добавить, Ctrl — убрать [B5] |
| Сквозь / по кругу | Alt+клик → меню [B6] | · | повторный клик по кругу; Ctrl+RMB → меню [U3][U4] | · | повторный клик по кругу [X3] | · | **повторный клик по кругу** + пункт «Под курсором ▸» в контекстном меню |
| Удалить | X (с вопросом), Del [B8] | · | Delete [U12] | · | Del [X5] | · | **Del** и **Backspace** (ноутбуки, macOS) |
| Дублировать | Shift+D [B16] | · | Ctrl+D [U12] | Ctrl+D; Shift+драг манипулятора [M4] | Shift+драг [X6] | Ctrl+W, Alt+драг гизмо [E3] | **Ctrl+D** (копия на месте) и **Shift+драг гизмо** (копия «на лету») |
| Скрыть / показать | H / Alt+H [B8] | · | H [U7] | Ctrl+H / Ctrl+Shift+H [M4] | Alt+H / Alt+U [X5] | H / Ctrl+H [E3] | **H / Alt+H** (H — у 3 из 6) |
| Инструменты | G/R/S (операторы), W — выбор [B8]; Q/W/E/R в Industry [B9] | S выбор, T/R/E [H2][H3] | Q/W/E/R [U2][U12] | Q/W/E/R [M4] | Q/W/E/R [X5] | Q/W/E/R [E3] | **Q выбор, W двигать, E вращать, R масштаб** (у 4 из 6 + Industry-раскладка Blender) |
| Ограничение осью | драг оси гизмо; X/Y/Z во время операции, Shift+X — плоскость [B10][B11] | драг оси/плоскости [H3] | драг оси/плоскости [U2] | драг оси/плоскости [M2] | драг оси/плоскости [X4] | драг оси/плоскости [E3] | **драг оси гизмо**; во время драга **X/Y/Z** (ось) и **Shift+X/Y/Z** (плоскость) — бонус из Blender |
| Привязка | зажать Ctrl; Shift+Tab — вкл./выкл. [B12] | зажать Ctrl [H3] | зажать Ctrl [U8] | зажать j [M4] | S — вкл./выкл. [X5] | X — вкл./выкл. [E3] | **зажать Ctrl** (у 3 из 6) + кнопка-магнит на панели |
| Локально / мир | меню, «,» [B16] | ПКМ по хэндлу [H3] | X [U12] | меню инструмента (W+LMB) [M4] | список на панели | иконка-глобус [E3] | **кнопка на панели + L** (общего стандарта нет, буква по смыслу) |
| Опорная точка | меню, «.» [B16] | Insert или « " » [H3] | Z: Pivot/Center [U12] | D или Insert [M3] | Affect Pivot Only [X5] | Alt+MMB-драг [E3] | **центр масс** по умолчанию; «Центр/опора» на панели; **D или Insert** — правка (Maya + Houdini) |
| Отмена / повтор | Ctrl+Z / Ctrl+Shift+Z [B8] | · | Ctrl+Z / Ctrl+Y [U12] | Ctrl+Z / Ctrl+Y [M4] | Ctrl+Z / Ctrl+Y [X5] | · | **Ctrl+Z**, повтор — **Ctrl+Y и Ctrl+Shift+Z** |
| Старт / пауза / стоп | Space [B16] | · | Ctrl+P, пауза Ctrl+Shift+P [U12] | Alt+V [M4] | / [X5] | Alt+P (Alt+S — симуляция), Esc — стоп [E4] | **Space** — старт/пауза; **Esc** — стоп и возврат сцены (как Unreal); Ctrl+P — синоним; «.» — один шаг |
| Сохранить результат симуляции | Object ▸ Rigid Body ▸ Apply Transformation | · | копировать компонент в Play → вставить значения | · | · | K — Keep Simulation Changes [E4] | **K** (как Unreal) и кнопка на полосе «ИГРА» |
| Поиск команды | F3 — Menu Search | Tab-меню в сетях | · | · | · | · | **Ctrl+K** (командная палитра, как VS Code): имя, путь в меню, клавиша |
| Виды сверху/спереди/сбоку | Numpad 7/1/3, Ctrl — обратные [B3] | меню вьюпорта [H1] | клик по конусам куба [U5] | Space (тап) — 4 вида [M4] | T/F/L/P [X5] | Alt+J/H/K/G [E5] | **Numpad 7/1/3** (Ctrl — обратные) + **клик по осям гизмо** (для ноутбуков) |
| Контекстное меню | RMB (при выборе ЛКМ); пай-меню на Q, Z, ` [B8][B14] | радиальные меню на клавишах [H4] | RMB [U6] | RMB — marking menu [M6] | RMB — quad menu [X7] | RMB [E1] | **ПКМ-клик = обычное список-меню**. Без пай-меню: их надо заучивать |
| Двойной клик | · | в режиме полёта — «прыжок» к геометрии [H1] | · | · | · | · | **двойной клик по объекту = F** |
| Полёт | Shift+` (WASD, Q/E) [B4] | M в инструменте View (WASD, Q/E) [H1] | RMB + WASD, Q/E [U1] | Fly Tool | Walkthrough (↑) [X5] | RMB + WASD, Q/E [E1] | **ПКМ + WASD**, Q/E — вниз/вверх, Shift — быстрее, колесо — скорость |

**Конфликты, которые мы разрешили:** R в Blender — вращение, у остальных — масштаб; X в Blender — удаление, в Unity — локально/мир, в
Unreal — привязка; Shift+драг гизмо в Blender — плоскость, в Max/Maya — копия. В нашей раскладке по умолчанию каждая клавиша
значит то, что она значит у большинства. Blender-значения живут в пресете «Как в Blender».

## 2. Схема мыши

Три лагеря: **Blender** — орбита на MMB [B1]; **Maya / Unity / Houdini / Unreal / Industry-раскладка Blender** — Alt + три
кнопки [M1][U1][H1][E1][B9]; **Unreal/Unity «игровой»** — RMB = осмотр/полёт [E1][U1]; 3ds Max — Alt+MMB [X1].

**По умолчанию («Простая»):**

| Кнопка | Без модификатора | С Alt (как в Maya/Unity/Unreal/Houdini) |
|---|---|---|
| ЛКМ | выбрать / рамка / гизмо | орбита |
| СКМ | панорама | панорама |
| ПКМ | драг — орбита вокруг точки под курсором; клик — меню; +WASD — полёт | зум (долли) |
| Колесо | зум к курсору | — |

Почему так: у каждой кнопки один «глагол» (выбрать, панорама, осмотр), камера не требует модификаторов, а Alt-комбинации
пользователей Maya/Unity/Unreal/Houdini работают без переключения пресета. Клик ПКМ и драг ПКМ различаются по порогу в 4 px,
как в Unity: там ПКМ — и меню [U6], и полёт [U1].

**Пресеты** (Настройки → Управление; при первом запуске спросим «Откуда вы пришли?»):

- **«Как в Blender»**: MMB — орбита, Shift+MMB — панорама, Ctrl+MMB — зум [B1]; G/R/S, X — удалить, Shift+D, Numpad . / Home,
  Alt+клик — меню выбора [B6].
- **«Как в Maya/Unity»**: камера только через Alt+кнопки; ПКМ-драг — осмотр на месте с WASD (как в Unity/Unreal) [U1][E1].

## 3. Гизмо

| Свойство | Как у всех | Наше решение |
|---|---|---|
| Цвета | X красный, Y зелёный, Z синий — Blender [B10], Unreal [E3], 3ds Max [X4], Unity [U2] | так же; при наведении ось становится жёлтой (3ds Max, Unreal [X4][E3]) |
| Плоскости | квадратики между осями: Blender, Unity, Maya, 3ds Max, Houdini [B10][U2][M2][X4][H3] | квадратики в цветах двух осей (3ds Max [X4]) |
| Центр | свободный сдвиг в плоскости экрана: Blender — белый круг, 3ds Max — центральный квадрат, Unity — Shift+центр [B10][X4][U2] | белый круг, сдвиг в плоскости экрана |
| Вращение | внешнее белое кольцо — вокруг оси взгляда; трекбол между кольцами [B10][U2][E3][X4][H3] | так же; во время вращения показываем сектор угла (3ds Max [X4]) |
| Масштаб | кубики на осях, центр — равномерно [U2][E3][X4] | так же |
| Размер | постоянный в пикселях; клавиши +/− (Maya [M4]), `*`/`&` (Houdini [H3]), −/= (3ds Max [X5]) | ~110 px при 100 % DPI; **+/−** |
| Модификаторы драга | Ctrl — шаг (привязка), Shift после нажатия — точнее [B8][B10][U8][H3] | так же; шаги по умолчанию: **0,1 м / 15° / 10 %** |
| Числа при драге | Blender — значения в заголовке и ввод числа с клавиатуры [B11]; 3ds Max и Unreal — угол в реальном времени [X4][E3] | подпись у курсора «Δ 0,40 м · X»; **ввод числа с клавиатуры** во время драга; Enter/ЛКМ — принять, Esc/ПКМ — отмена (Blender [B1]) |

## 4. Коллайдер и видимая геометрия

- **Houdini (RBD Packed / Bullet):** форма задаётся параметром *Geometry Representation*: Convex Hull (по умолчанию), Concave,
  Box, Capsule, Cylinder, Sphere, Compound, Plane. *Show Guide Geometry* показывает форму столкновений вместе с зазором
  (*Collision Padding*). Есть цвета *Color* и *Deactivated Color*: второй — для уснувших тел [H5][H6]. У RBD Bullet Solver
  третий вход — упрощённый proxy, четвёртый — геометрия столкновений, переключатель *Show Collision Shape*. Клавиши 1/2/3
  переключают Geometry / Constraint / Proxy [H7].
- **Unity:** коллайдер — отдельный компонент, невидимый в игре и не обязанный повторять меш. Есть примитивы, Mesh Collider
  (Convex — до 255 треугольников) и составные коллайдеры [U10]. В сцене коллайдер виден как каркасное гизмо [U9], кнопка
  *Edit Collider* даёт ручки размера [U10]. Physics Debugger раскрашивает статические, триггеры, rigidbody, кинематические и
  спящие тела разными цветами [U11].
- **Unreal:** *Simple* — примитивы и выпуклые оболочки, *Complex* — треугольная сетка. При *Use Complex As Simple* объект нельзя
  симулировать [E6]. В Static Mesh Editor есть меню Collision (Box/Sphere/Capsule, k-DOP, *Auto Convex Collision*) и кнопка
  показа коллизий [E7][E8]. Во вьюпорте есть режим *Player Collision* с раскраской по типу [E9].

**Наш UI** — подраздел «Коллайдер» внутри роли «Твёрдое тело»:

- **Форма:** `Авто` (по умолчанию) · Коробка · Сфера · Капсула · Выпуклая оболочка · Сетка (только для неподвижных).
  «Авто» для примитива берёт тот же примитив, для остального — выпуклую оболочку (как Convex Hull по умолчанию в Houdini [H5]).
  Рядом серым написано, что выбрано: «Авто → капсула».
- **Показ:** кнопка-глаз «Показать коллайдер» у объекта + общий оверлей вьюпорта. Каркас рисуется поверх модели. Цвет по
  состоянию: динамическое — зелёный, статическое — серый, кинематическое — голубой, спящее — тусклый (Unity [U11], Houdini [H5]).
- **Правка:** «Редактировать коллайдер» включает ручки размера и смещения (Unity *Edit Collider*, Unreal [U10][E7]). Зазор и
  прочее — в свёрнутом «Дополнительно».
- **Подсказки:** «Сетка» на динамическом теле → предупреждение со ссылкой на выпуклую оболочку (как ограничение Unreal [E6]).

## 5. Помощь новичку

Что делают пакеты: **Blender** — навигационный гизмо в правом верхнем углу: драг — орбита, клик по оси — вид, кнопки
зума/панорамы/проекции [B2]; строка состояния показывает, что делают кнопки мыши и клавиши активного инструмента, а в режиме
полёта — его клавиши [B13][B4]. **Unity** — куб ориентации: клик по конусу — вид, клик по кубу — перспектива/орто, Shift+клик —
вернуть обычный вид [U5]. **3ds Max** — жёлтый контур под курсором до клика, синий — у выделенного [X3]. **Houdini** —
радиальные меню, которые учат «направлениям» [H4]. **Unreal** — клавиша G прячет все редакторские значки [E1].

**10 правил:**

1. **ЛКМ никогда не двигает камеру.** ЛКМ — выбор и гизмо, ПКМ — осмотр, СКМ — панорама, колесо — зум к курсору.
2. **Alt+кнопки всегда работают** — мышечная память Maya/Unity/Unreal/Houdini не ломается.
3. **Одна клавиша — одно действие, как у большинства.** Спорные значения (R, X, Shift+драг) — только в пресетах.
4. **Каждая подсказка показывает клавишу** («Переместить — W»), и в меню клавиши написаны справа.
5. **Строка состояния всегда отвечает на вопрос «что будет, если нажать»**: «ЛКМ: выбрать · ПКМ: осмотр · СКМ: панорама», а во
   время драга — «X/Y/Z: ось · Ctrl: шаг · Esc: отмена» (Blender [B13]).
6. **Навигационный гизмо в углу** — гибрид Blender и Unity: драг — орбита, клик по оси — вид, клик в центр — перспектива/орто,
   двойной клик — домой. Вся навигация доступна без клавиатуры и без Numpad.
7. **«Потерялся» = F.** Без выделения F показывает всю сцену; двойной клик по объекту = F.
8. **Подсветка до клика:** контур объекта и жёлтая ось под курсором. Видно, что будет выбрано и какая ось схвачена.
9. **Всё отменяется, симуляция не портит сцену:** Ctrl+Z для любого действия; Esc/Стоп возвращает сцену в состояние до
   Space (как стоп в Unreal [E4]).
10. **Умные умолчания и никаких режимов:** коллайдер «Авто», привязка выключена, зум к курсору, нет Object/Edit-режимов и
    пай-меню; при первом запуске — выбор пресета «Простая / Как в Blender / Как в Maya/Unity».

## 6. Много объектов сразу

**Shift + гизмо = клонировать** (3ds Max: Shift+драг манипулятора [X6]; Maya [M4]). Важен момент **начала** драга:

| Когда нажат Shift | Что делает |
|---|---|
| зажат при нажатии ЛКМ на ручку гизмо (двигать, вращать, масштаб) | **клонирование**: выбранное остаётся, за гизмо едет копия; отпустили — окошко «Клонировать» |
| нажат уже во время драга | **точнее** (мышь в 10 раз медленнее), как и раньше |
| зажат при драге самого объекта, не ручки | движение по полу |

Окошко «Клонировать» у курсора: **сколько копий** (1–1000), **● Копия ○ Экземпляр ○ Массив**, OK / Отмена (Enter / Esc,
щелчок мимо — тоже отмена). Копии повторяют тот же шаг от оригинала: сдвиг d → d, 2d, 3d…; поворот на a → a, 2a… вокруг центра
гизмо; масштаб s → s, s², s³… Выбрано несколько объектов — у каждого свой ряд. Всё клонирование — **один Ctrl+Z**.

| Вид | Что получится | Как в пакетах |
|---|---|---|
| Копия | независимые объекты (группа — со всем содержимым) | Copy в 3ds Max [X6] |
| Экземпляр | общая форма и компоненты: правка плотности или коллайдера одного меняет все; ⧉ в списке | Instance в 3ds Max [X6] |
| Массив | один объект «Массив»: число копий и шаг меняются одним числом; оригинал — его скрытый образец | Array-модификатор Blender, copy-to-points Houdini |

Масштаб повторяют только копии: у экземпляров и массива размер общий, эти пункты тогда серые.

| Действие | Где | Клавиши |
|---|---|---|
| сгруппировать / разгруппировать | Правка, меню ПКМ | Ctrl+G (Group в Maya [M4]) / Ctrl+Shift+G |
| выбрать группу, затем объект в ней | щелчок по объекту, ещё щелчок | — |
| склеить группу в одно тело | карточка «Группа» | — |
| сделать массивом | меню ПКМ, Правка → «Сделать массивом…» | — |
| ряд / сетка / круг, сколько, шаг, поворот, разброс | карточка «Массив» | — |
| разобрать массив на объекты | карточка «Массив» | — |
| отвязать экземпляр | карточка объекта, меню ПКМ | — |
| выбрать все такие же / с компонентом / инвертировать | меню ПКМ и Правка | — |

**Правка сразу многих.** Выбрано несколько: поле, в котором значения разные, показывает «—»; число, введённое в поле, получают
все, остальные поля у каждого свои. Видны карточки компонентов, которые есть у всех; «+ Добавить компонент» и плитки ролей
добавляют всем.

## 7. Свет и камеры

Свет и камеры — такие же объекты сцены: имя, место, поворот, цвет, глаз и замок в блоке «Объект». Физика их не видит, они
для глаза и для скриншотов.

| Что | Где | Как |
|---|---|---|
| солнце, лампа, прожектор | панель сверху: «Свет ▾» | солнце — высоко, светит вниз наискось, с тенями; лампа — на 1,5 м над выбранным (или над центром); прожектор — сверху сбоку, нацелен на выбранное |
| камера | панель сверху: «Камера» | встаёт там, где сейчас вид редактора, и смотрит в центр сцены |
| выбрать | щелчок по каркасу | солнце — диск и стрелка; лампа — колба из трёх колец (выбрана — ещё круг её дальности); прожектор — конус, мягкий край бледнее; камера — пирамида вида и треугольник «верх» |
| двигать, вращать | гизмо W / E, как у фигур | масштаб их не меняет; солнце и прожектор светят вдоль своей оси −y, камера смотрит вдоль −z |
| вид, яркость, дальность, конус, мягкий край, тени | карточка «Свет» | тени пока отбрасывает только первое солнце с галочкой (карта теней, мягкий край 3 × 3) |
| угол обзора, ближняя и дальняя плоскости | карточка «Камера» | — |
| смотреть через камеру | карточка «Камера» → «Смотреть через камеру», Вид → «Камеры ▸» | вид становится камерой; вращение, панорама, колесо и полёт двигают саму камеру (как «Lock Camera to View» в Blender [B3]); **Esc** — назад к виду редактора; запертая замком камера не двигается |
| скриншот через камеру | Файл → «Скриншот…» при виде через камеру; ключ `--camera` | снимок из активной камеры сцены |

В игре каркасы света и камер не рисуются (кроме выбранного); свет продолжает светить, правка света во время игры видна со
следующего кадра. Без источников света сцена освещена как раньше — светом редактора сверху слева.

## Источники

- [B1] https://docs.blender.org/manual/en/latest/editors/3dview/navigate/navigation.html
- [B2] https://docs.blender.org/manual/en/latest/editors/3dview/navigate/introduction.html
- [B3] https://docs.blender.org/manual/en/latest/editors/3dview/sidebar.html (View → Lock Camera to View)
- [B3] https://docs.blender.org/manual/en/latest/editors/3dview/navigate/viewpoint.html
- [B4] https://docs.blender.org/manual/en/latest/editors/3dview/navigate/walk_fly.html
- [B5] https://docs.blender.org/manual/en/latest/interface/selecting.html
- [B6] https://docs.blender.org/manual/en/latest/editors/3dview/selecting.html
- [B7] https://docs.blender.org/manual/en/latest/scene_layout/object/selecting.html
- [B8] https://docs.blender.org/manual/en/latest/interface/keymap/blender_default.html
- [B9] https://docs.blender.org/manual/en/latest/interface/keymap/industry_compatible.html
- [B10] https://docs.blender.org/manual/en/latest/editors/3dview/display/gizmo.html
- [B11] https://docs.blender.org/manual/en/latest/scene_layout/object/editing/transform/control/axis_locking.html
- [B12] https://docs.blender.org/manual/en/latest/editors/3dview/controls/snapping.html
- [B13] https://docs.blender.org/manual/en/latest/interface/window_system/status_bar.html
- [B14] https://docs.blender.org/manual/en/latest/interface/controls/buttons/menus.html
- [B15] https://docs.blender.org/manual/en/latest/editors/preferences/navigation.html
- [B16] https://docs.blender.org/manual/en/latest/scene_layout/object/editing/duplicate.html ·
  https://docs.blender.org/manual/en/latest/editors/3dview/controls/pivot_point/index.html ·
  https://docs.blender.org/manual/en/latest/editors/3dview/controls/orientation.html ·
  https://docs.blender.org/manual/en/latest/editors/preferences/keymap.html (Spacebar Action)
- [H1] https://www.sidefx.com/docs/houdini/basics/view.html
- [H2] https://www.sidefx.com/docs/houdini/basics/select.html
- [H3] https://www.sidefx.com/docs/houdini/basics/handles.html
- [H4] https://www.sidefx.com/docs/houdini/basics/radialmenus.html
- [H5] https://www.sidefx.com/docs/houdini/nodes/dop/rbdpackedobject.html
- [H6] https://www.sidefx.com/docs/houdini/nodes/sop/rbdconfigure.html
- [H7] https://www.sidefx.com/docs/houdini/nodes/sop/rbdbulletsolver.html
- [U1] https://docs.unity3d.com/Manual/SceneViewNavigation.html
- [U2] https://docs.unity3d.com/Manual/PositioningGameObjects.html
- [U3] https://docs.unity3d.com/Manual/SelectGameObjects.html
- [U4] https://docs.unity3d.com/Manual/SelectionPiercingMenu.html
- [U5] https://docs.unity3d.com/Manual/overlay-orientation.html
- [U6] https://docs.unity3d.com/Manual/SceneViewContextMenu.html
- [U7] https://docs.unity3d.com/Manual/SceneVisibility.html
- [U8] https://docs.unity3d.com/Manual/SnapIncrements.html
- [U9] https://docs.unity3d.com/Manual/GizmosMenu.html · https://docs.unity3d.com/6000.6/Documentation/Manual/gizmos-introduction.html
- [U10] https://docs.unity3d.com/Manual/CollidersOverview.html · https://docs.unity3d.com/Manual/collider-shapes-introduction.html ·
  https://docs.unity3d.com/Manual/class-BoxCollider.html · https://docs.unity3d.com/Manual/class-MeshCollider.html
- [U11] https://docs.unity3d.com/Manual/PhysicsDebugVisualization.html
- [U12] https://docs.unity3d.com/2018.4/Documentation/Manual/UnityHotkeys.html (последняя версия с полной таблицей клавиш)
- [M1] https://help.autodesk.com/view/MAYAUL/2025/ENU/?guid=GUID-941480C1-9BDA-4DB3-8EA8-113A0D6FAF1F
- [M2] https://help.autodesk.com/view/MAYAUL/2025/ENU/?guid=GUID-ABF1A141-7860-4234-889B-00278F20B9A3
- [M3] https://help.autodesk.com/view/MAYAUL/2025/ENU/?guid=GUID-150B390E-840B-4FE3-B8E9-8DEBCE7CEC97
- [M4] https://help.autodesk.com/view/MAYAUL/2025/ENU/?guid=GUID-30CACC9D-8FBE-4B85-8A8F-C5ADF32DDD4E
- [M5] https://help.autodesk.com/view/MAYAUL/2025/ENU/?guid=GUID-5C5F2B9B-BE16-4C49-B72B-70EA7CB4C688
- [M6] https://help.autodesk.com/view/MAYAUL/2025/ENU/?guid=GUID-8BA1A3AA-4C44-4779-8B22-0AAE3627E8EB
- [X1] https://help.autodesk.com/view/3DSMAX/2025/ENU/?guid=GUID-D65DE5FD-F859-4F66-9E14-F9A5C1016411 ·
  https://help.autodesk.com/view/3DSMAX/2025/ENU/?guid=GUID-6DA741AB-9941-4957-903A-4B998C1C65E7
- [X2] https://help.autodesk.com/view/3DSMAX/2025/ENU/?guid=GUID-CCD2C70B-52B4-4BF0-A5C0-74ECC591ACDD
- [X3] https://help.autodesk.com/view/3DSMAX/2025/ENU/?guid=GUID-1962BD49-AAD7-43FA-871E-5C2742C047B8
- [X4] https://help.autodesk.com/view/3DSMAX/2025/ENU/?guid=GUID-D97C423B-1AD4-46EA-892B-3A807823892C
- [X5] https://help.autodesk.com/view/3DSMAX/2025/ENU/?guid=GUID-A73E1B09-7BFE-4A22-8153-1D3D2237B8E9
- [X6] https://help.autodesk.com/view/3DSMAX/2025/ENU/?guid=GUID-03F84C88-6EEC-42A4-BCB9-C487243E7B99
- [X7] https://help.autodesk.com/view/3DSMAX/2025/ENU/?guid=GUID-3A803F43-6F87-40FB-97CA-33844078AF9A
- [E1] https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine
- [E2] https://dev.epicgames.com/documentation/en-us/unreal-engine/selecting-actors-in-unreal-engine
- [E3] https://dev.epicgames.com/documentation/en-us/unreal-engine/transforming-actors-in-unreal-engine
- [E4] https://dev.epicgames.com/documentation/en-us/unreal-engine/playing-and-simulating-in-unreal-engine
- [E5] https://dev.epicgames.com/documentation/en-us/unreal-engine/using-editor-viewports-in-unreal-engine
- [E6] https://dev.epicgames.com/documentation/en-us/unreal-engine/simple-versus-complex-collision-in-unreal-engine
- [E7] https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-collisions-with-static-meshes-in-unreal-engine
- [E8] https://dev.epicgames.com/documentation/en-us/unreal-engine/static-mesh-editor-ui-in-unreal-engine
- [E9] https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-modes-in-unreal-engine
