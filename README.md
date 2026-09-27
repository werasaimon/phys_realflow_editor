# PhysRealFlow Editor

Редактор-просмотрщик для [PhysRealFlow](https://github.com/werasaimon/phys_realflow): окно OpenGL 3.0,
панели параметров каждого решателя, графики любого канала отладчика `Probe`, скриншоты и CSV из
командной строки. Физика — в SDK, подключённом как git-сабмодуль `extern/phys_realflow`; здесь
только Qt 6 и отрисовка.

## Сборка (Windows, Qt 6.7.3 MinGW)

```bat
git clone --recurse-submodules https://github.com/werasaimon/phys_realflow_editor.git
cd phys_realflow_editor
set PATH=C:\Qt\Tools\mingw1120_64\bin;C:\Qt\Tools\Ninja;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/mingw_64
cmake --build build -j
run.cmd --preset 27
```

Модули Qt: Core, Gui, Widgets, OpenGL, OpenGLWidgets, Charts. Обновить SDK до последнего коммита:
`git submodule update --remote extern/phys_realflow`.

## Командная строка

| Ключ | Смысл |
|---|---|
| `--preset N` | загрузить сцену `N` из реестра `samples/` SDK и запустить |
| `--frames M` | сколько кадров посчитать перед снимком (по умолчанию 120) |
| `--screenshot file.png` | сохранить PNG окна и выйти |
| `--csv file` | сохранить все каналы `Probe` и графики сцены в CSV и выйти |
| `--size WxH` | размер окна (по умолчанию `1600x950`) |
| `--software-gl` | рендер на процессоре (Mesa llvmpipe) |

## Что где

```
src/   MainWindow (панели, меню сцен из реестра SDK), Viewport (OpenGL: тела, частицы, срезы, объём дыма,
       силовые линии, отладочная отрисовка Probe), FluidSurfaceRenderer (экранная вода), PlotPanel
       (графики любого канала), SimController (поток расчёта), ParamForm, OrbitCamera, ViewportTools
extern/phys_realflow   SDK: src/ (решатели), samples/ (сцены), tests/, docs/
```

Документация физики, формулы и таблицы проверок — в SDK: `extern/phys_realflow/docs/`.
