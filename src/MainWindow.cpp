// The main window (see MainWindow.h): the 3D view in the middle, the scene builder on the right,
// the solver parameters and the visualisation on the left under "Эксперт", the readings and plots
// at the bottom under "Графики". This file builds the actions, the menus, the parameter forms and
// the docks, and runs the automation of the command line (a scene, frames, a screenshot, a CSV).
// Other parts of the window live in MainWindowStart.cpp (the first minute), MainWindowControls.cpp
// (keys, mouse schemes, many objects), MainWindowToolbar.cpp (the top bar) and MainWindowPlay.cpp
// (the play banner, K, Ctrl+K, the white-window check).
#include "MainWindow.h"
#include "RuPlural.h"

#include "ParamForm.h"
#include "RoleBar.h"
#include "PlotPanel.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include "core/Probe.h"
#include "samples/Models.h"
#include "samples/Samples.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <random>

using namespace rf;
using Snap = RenderSnapshot;

static const QStringList kShapes = {"Нет", "Сфера", "Куб", "Цилиндр", "Крыло NACA", "Обтекаемое тело",
                                    "Эллипсоид", "Конус", "Импортированная модель"};
static const QStringList kFields = {"Скорость |V|", "Давление p", "Коэф. давления Cp", "Завихренность |ω|",
                                    "Дым (плотность)", "Температура", "Скорость Vx", "Скорость Vy",
                                    "Магнитное поле |B|", "Плотность тока |J|"};
static const QStringList kBC = {"Стенка", "Вход потока", "Выход (p = 0)"};

MainWindow::MainWindow() {
    setWindowTitle("PhysRealFlow");
    ctrl_ = std::make_unique<SimController>();

    view_ = new Viewport(this);
    view_->setBrushRadius(brush_.radius);
    setCentralWidget(view_);
    connectViewport();
    buildActions();
    buildParameterDock();
    buildVisualDock();
    buildResultsDock();
    // The right column (scene list, inspector) runs the full height, like the panels of a 3D package.
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    buildSceneBuilderDock();
    connectSceneBuilder();
    buildLayoutActions();
    buildMainToolbar();
    buildToolShelf();
    buildSceneMenu();
    buildEditMenu();
    buildControlsMenu();
    buildCamerasMenu();
    buildCameraPicker();
    buildPlayExtras();
    connectViewportControls();
    setExpertMode(false);   // a beginner's screen: the builder and a big 3D view
    setGraphsVisible(false);

    status_ = new QLabel;
    statusBar()->addPermanentWidget(status_);
    onModeChanged();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::poll);
    timer_->start(15);
    poll();
}

// What the 3D view asks of the simulation: a stir of the gas, the mouse joint (pick with the cursor
// ray, drag the target, release), and a restart without the GPU when OpenGL 3 is missing.
void MainWindow::connectViewport() {
    connect(view_, &Viewport::disturbanceRequested, this, &MainWindow::onDisturbance);
    connect(view_, &Viewport::openGLUnsupported, this, [this](QString renderer) {
        auto answer = QMessageBox::question(
            this, "OpenGL",
            QString("Видеокарта или драйвер не поддерживает OpenGL 3.0 (%1).\n\n"
                    "Перезапустить программу в программном режиме? Всё будет рисоваться процессором "
                    "(медленнее, но работает на любом ПК).").arg(renderer));
        if (answer != QMessageBox::Yes) return;
        QStringList args = QCoreApplication::arguments().mid(1);
        args << "--software-gl";
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
        qApp->quit();
    });
    connect(view_, &Viewport::grabStarted, this, [this](int body, Vector3 p) {
        ctrl_->post([body, p](Simulation& s) { s.rigid.grab(body, p); });
    });
    connect(view_, &Viewport::particleGrabStarted, this, [this](Vector3 p) {
        ctrl_->post([p](Simulation& s) { s.particles.grab(p); });
    });
    connect(view_, &Viewport::grabMoved, this, [this](Vector3 t) {
        ctrl_->post([t](Simulation& s) {
            s.rigid.setGrabTarget(t);
            s.particles.setGrabTarget(t);
        });
    });
    connect(view_, &Viewport::grabReleased, this, [this] {
        ctrl_->post([](Simulation& s) {
            s.rigid.releaseGrab();
            s.particles.releaseGrab();
        });
    });
}

MainWindow::~MainWindow() {
    timer_->stop();
    ctrl_.reset(); // stop the simulation thread before the widgets go away
}

// ---------------------------------------------------------------------------
// Toolbar & menu
// ---------------------------------------------------------------------------
void MainWindow::buildActions() {
    buildRunActions();
    buildMenus();
}

// Пуск, Пауза, Стоп, Шаг: the actions the toolbar, the menu and the big buttons on the view share.
void MainWindow::buildRunActions() {
    // Edit -> play -> stop, as in a game engine: the builder's scene is only simulated while it plays.
    playAct_ = new QAction(controlIcon(ControlIcon::Play, 48), "Пуск", this);
    playAct_->setShortcut(Qt::Key_Space);
    playAct_->setToolTip("Пуск (пробел): физика оживает — сцена строится из того, что нарисовано");
    connect(playAct_, &QAction::triggered, this, &MainWindow::togglePlay);
    playAct_->setObjectName("actionPlay");
    pauseAct_ = new QAction(controlIcon(ControlIcon::Pause, 48), "Пауза", this);
    pauseAct_->setToolTip("Пауза (пробел)");
    connect(pauseAct_, &QAction::triggered, this, [this] { builder_->pause(); });
    stopAct_ = new QAction(controlIcon(ControlIcon::Stop, 48), "Стоп", this);
    stopAct_->setShortcut(QKeySequence("Shift+Space"));
    stopAct_->setToolTip("Стоп (Shift+пробел): назад к правке — сцена такая, какой её нарисовали");
    connect(stopAct_, &QAction::triggered, this, [this] { builder_->stop(); });

    stepAct_ = new QAction(controlIcon(ControlIcon::Step, 48), "Шаг", this);
    stepAct_->setShortcut(Qt::Key_Period);
    stepAct_->setToolTip("Один кадр (.)");
    connect(stepAct_, &QAction::triggered, this, [this] { builder_->step(); });
}

// The menu bar: Файл (import, export, screenshot), Симуляция, Вид (filled later), Справка.
void MainWindow::buildMenus() {
    auto* importAct = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "Импорт модели…", this);
    importAct->setShortcut(QKeySequence("Ctrl+I")); // Ctrl+O opens a scene (menu "Сцена")
    connect(importAct, &QAction::triggered, this, &MainWindow::importMesh);
    auto* shotAct = new QAction("Скриншот…", this);
    shotAct->setShortcut(QKeySequence("Ctrl+P"));
    connect(shotAct, &QAction::triggered, this, &MainWindow::screenshot);
    auto* csvAct = new QAction("Экспорт графиков CSV…", this);
    csvAct->setShortcut(QKeySequence("Ctrl+E"));
    connect(csvAct, &QAction::triggered, this, &MainWindow::exportCsv);
    auto* loadsAct = new QAction("Экспорт нагрузок по полигонам CSV…", this);
    loadsAct->setToolTip("Cp, Cf и сила на каждом треугольнике тела в аэротрубе");
    connect(loadsAct, &QAction::triggered, this, &MainWindow::exportSurfaceLoads);

    buildSamplesMenu();

    auto* file = menuBar()->addMenu("&Файл");
    file->addAction(importAct);
    file->addAction(csvAct);
    file->addAction(loadsAct);
    file->addAction(shotAct);
    file->addSeparator();
    file->addAction("Выход", QKeySequence::Quit, this, &QWidget::close);

    auto* sim = menuBar()->addMenu("&Симуляция");
    sim->setObjectName("simulationMenu");
    for (QAction* a : {playAct_, pauseAct_, stopAct_, stepAct_}) sim->addAction(a);
    auto* rt = sim->addAction("Не быстрее реального времени");
    rt->setCheckable(true);
    rt->setChecked(true);
    connect(rt, &QAction::toggled, this, [this](bool on) { ctrl_->setRealtimeLimit(on); });

    auto* viewMenu = menuBar()->addMenu("&Вид");
    viewMenu->setObjectName("viewMenu");
    viewMenu->addSeparator();

    auto* help = menuBar()->addMenu("&Справка");
    help->setObjectName("helpMenu");
    help->addAction("Управление и методы", QKeySequence::HelpContents, this, &MainWindow::showHelp);
    help->addAction("О программе", this, &MainWindow::showAbout);
}

// The ready-made scenes of the SDK (samples/Samples.h), one submenu per category; the running one
// is ticked.
void MainWindow::buildSamplesMenu() {
    samplesMenu_ = new QMenu("Готовые сцены", this);
    samplesGroup_ = new QActionGroup(this);
    samplesGroup_->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    QMenu* category = nullptr;
    std::string categoryName;
    for (const SampleEntry& e : samples()) {
        if (!category || e.category != categoryName) {
            categoryName = e.category;
            category = samplesMenu_->addMenu(QString::fromStdString(categoryName));
        }
        QAction* a = category->addAction(QString::fromStdString(e.name));
        a->setCheckable(true);
        samplesGroup_->addAction(a);
        const int p = int(e.id);
        connect(a, &QAction::triggered, this, [this, p] {
            builder_->showSample(); // the builder steps aside while a ready-made scene runs
            ctrl_->post([p](Simulation& s) { loadSample(s, p); });
            ctrl_->setRunning(true);
        });
    }
}

void MainWindow::buildLayoutActions() {
    expertAct_ = new QAction("Эксперт", this);
    expertAct_->setCheckable(true);
    expertAct_->setToolTip("Все параметры решателей, визуализация и кисть — для тех, кто знает, что крутит");
    connect(expertAct_, &QAction::toggled, this, &MainWindow::setExpertMode);
    graphsAct_ = new QAction("Графики", this);
    graphsAct_->setCheckable(true);
    graphsAct_->setShortcut(QKeySequence("F9")); // Ctrl+G is Group, as in Maya
    graphsAct_->setToolTip("Показания и графики внизу (F9); без них главные числа — в строке состояния");
    connect(graphsAct_, &QAction::toggled, this, &MainWindow::setGraphsVisible);
    collidersAct_ = new QAction(roleIcon(RoleIcon::Collider, 20), "Коллайдеры", this);
    collidersAct_->setCheckable(true);
    collidersAct_->setToolTip("Показать, чем сталкивается каждый объект: тонкий зелёный каркас. "
                              "Выбранный объект показывает свой всегда");
    connect(collidersAct_, &QAction::toggled, builder_, &SceneBuilder::setShowAllColliders);
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    viewMenu->addSeparator();
    viewMenu->addAction(collidersAct_);
    viewMenu->addAction(graphsAct_);
    viewMenu->addAction(expertAct_);
}

void MainWindow::setExpertMode(bool on) {
    view_->setShowStats(on); // the time step and frame rate: numbers for the curious
    for (const char* name : {"paramsDock", "visDock"})
        if (auto* dock = findChild<QDockWidget*>(name)) dock->setVisible(on);
    expertAct_->setChecked(on);
}

void MainWindow::setGraphsVisible(bool on) {
    if (auto* dock = findChild<QDockWidget*>("resultsDock")) dock->setVisible(on);
    graphsAct_->setChecked(on);
}

void MainWindow::togglePlay() {
    if (builder_->mode() == SceneBuilder::Mode::Playing) builder_->pause();
    else builder_->play();
}

QString MainWindow::modeText() const {
    if (builder_->editing()) return "Правка — физика стоит. ▶ Пуск (пробел) оживит сцену";
    return builder_->mode() == SceneBuilder::Mode::Playing ? "Симуляция идёт — роли меняются на лету" : "Пауза";
}

// Edit mode and pause share the viewport's edit tools (select, gizmo); on pause they work on the
// simulation as it stands, and a drag reaches it on release.
void MainWindow::onModeChanged() {
    const bool edit = builder_->editing(), gizmo = builder_->gizmoAllowed();
    const SceneBuilder::Mode m = builder_->mode();
    view_->setEditMode(gizmo);
    view_->setAuthoringScene(!builder_->showsSample());
    view_->setEditCaption(edit ? "Правка: физика стоит. Двигайте, вращайте, масштабируйте; ▶ Пуск оживит сцену."
                               : "Пауза: гизмо двигает объект там, где он сейчас; роли меняются на лету. ■ Стоп — к исходной сцене.");
    playAct_->setEnabled(m != SceneBuilder::Mode::Playing);
    pauseAct_->setEnabled(m == SceneBuilder::Mode::Playing);
    stopAct_->setEnabled(!edit);
    for (QAction* a : toolGroup_->actions()) a->setEnabled(gizmo);
    localAct_->setEnabled(gizmo);
    status_->setText(modeText());
    updatePlayOverlay();
}

// The right click of the edit mode, as the quad menu of 3ds Max: an object under the cursor that is
// not selected is selected first (the same click rules), then the menu opens at the cursor.
void MainWindow::showEditContextMenu(const QPointF& pos) {
    const uint32_t under = view_->pickEntity(view_->camera().screenRay(pos, view_->size()));
    if (under && !builder_->drawnSelected(under)) view_->clickSelect(pos, false);
    QMenu* menu = buildEditContextMenu();
    menu->exec(view_->mapToGlobal(pos.toPoint()));
    menu->deleteLater();
}

QMenu* MainWindow::buildEditContextMenu() {
    auto* menu = new QMenu(this);
    for (QAction* a : toolGroup_->actions()) menu->addAction(a); // the toolbar's own actions: one state
    menu->addSection("Система координат");
    auto* space = new QActionGroup(menu);
    const struct { const char* text; bool local; } spaces[] = {{"Мировая (World)", false}, {"Локальная (Local)", true}};
    for (const auto& s : spaces) {
        QAction* a = menu->addAction(s.text);
        a->setCheckable(true);
        a->setChecked(localAct_->isChecked() == s.local);
        a->setShortcut(Qt::Key_L);
        space->addAction(a);
        const bool local = s.local;
        connect(a, &QAction::triggered, this, [this, local] { localAct_->setChecked(local); });
    }
    if (builder_->selectedId()) addObjectActions(menu);
    return menu;
}

void MainWindow::addObjectActions(QMenu* menu) {
    const uint32_t id = builder_->selectedId();
    const rf::SceneObject* o = builder_->object(id);
    if (!o) return;
    const int n = int(builder_->selection().size());
    menu->addSection(n > 1 ? "Выбрано: " + ruPlural(n, "объект", "объекта", "объектов") : builder_->objectTitle(id));
    menu->addAction("Показать", QKeySequence(Qt::Key_F), this, [this] { builder_->frameSelected(); });
    menu->addAction("Дублировать", duplicateAct_->shortcut(), this, [this] { builder_->duplicateSelected(); });
    menu->addAction("Удалить", deleteAct_->shortcut(), this, [this] { builder_->removeSelected(); });
    menu->addAction(o->visible ? "Скрыть" : "Показать объект", QKeySequence(Qt::Key_H), this, [this, id] { builder_->toggleVisible(id); });
    menu->addAction(o->locked ? "Разблокировать" : "Заблокировать", this, [this, id] { builder_->toggleLocked(id); });
    addManyActions(menu);
    addSelectHelpers(menu);
    const rf::Entity* e = nullptr;
    for (const rf::Entity& x : builder_->graph().entities)
        if (x.id == builder_->masterOf(id)) e = &x;
    if (!e) return; // a group or an array has no components of its own
    QMenu* roles = menu->addMenu(roleIcon(RoleIcon::Rigid, 20), "Роль");
    for (int k = 0; k < int(RoleIcon::Count); ++k) {
        const RoleIcon role = RoleIcon(k);
        if (k == 4) roles->addSeparator(); // made of | also does
        QAction* a = roles->addAction(roleIcon(role, 20), roleName(role), this, [this, role] { builder_->toggleRole(role); });
        a->setCheckable(true);
        a->setChecked(roleEnabled(*e, role));
        a->setEnabled(!(role == RoleIcon::Cloth && e->shape == rf::ShapeKind::Mesh));
    }
}

void MainWindow::setTool(GizmoMode m) {
    view_->gizmo().setMode(m);
    view_->gizmo().setHover(GizmoHandle::None);
    view_->update();
}

// The tool shelf at the left edge of the 3D view, as in Blender: select, move, rotate, scale, and
// whether the gizmo's axes are the world's or the object's own.
void MainWindow::buildToolShelf() {
    auto* tb = new QToolBar("Инструменты", this);
    tb->setObjectName("toolShelf");
    tb->setMovable(false);
    tb->setIconSize(QSize(28, 28));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::LeftToolBarArea, tb);
    toolGroup_ = new QActionGroup(this);
    struct Tool { GizmoMode mode; ControlIcon icon; const char* name; const char* id; const char* key; const char* tip; };
    const Tool tools[] = {
        {GizmoMode::Select, ControlIcon::ToolSelect, "Выбор", "toolSelect", "Q", "Выбор (Q): щелчок выбирает, ещё щелчок туда же — то, что позади"},
        {GizmoMode::Translate, ControlIcon::ToolMove, "Перемещение", "toolMove", "W", "Перемещение (W): стрелки — по оси, квадраты — в плоскости, центр — свободно; Ctrl — шаг 0,1 м"},
        {GizmoMode::Rotate, ControlIcon::ToolRotate, "Вращение", "toolRotate", "E", "Вращение (E): кольца — вокруг оси, внешнее — вокруг взгляда; Ctrl — шаг 15°"},
        {GizmoMode::Scale, ControlIcon::ToolScale, "Масштаб", "toolScale", "R", "Масштаб (R): кубики — по оси, центр — целиком; Ctrl — шаг 10 %"}};
    for (const Tool& t : tools) {
        QAction* a = tb->addAction(controlIcon(t.icon, 28), t.name);
        a->setObjectName(t.id);
        a->setCheckable(true);
        a->setShortcut(QKeySequence(t.key));
        a->setToolTip(t.tip);
        a->setChecked(t.mode == GizmoMode::Translate);
        toolGroup_->addAction(a);
        const GizmoMode m = t.mode;
        connect(a, &QAction::triggered, this, [this, m] { setTool(m); });
    }
    tb->addSeparator();
    localAct_ = tb->addAction("Мир");
    localAct_->setObjectName("toolLocal");
    localAct_->setCheckable(true);
    localAct_->setShortcut(Qt::Key_L);
    localAct_->setToolTip("Оси гизмо (L): «Мир» — оси сцены, «Свои» — оси самого объекта");
    connect(localAct_, &QAction::toggled, this, [this](bool on) {
        view_->gizmo().setLocal(on);
        localAct_->setText(on ? "Свои" : "Мир");
        view_->update();
    });
    setTool(GizmoMode::Translate);
}

// ---------------------------------------------------------------------------
// Parameter forms. Each form shows the few parameters people change most; everything else sits
// in its collapsed "Дополнительно" section. Rows marked * restart the scene.
// ---------------------------------------------------------------------------
QGroupBox* MainWindow::addGroup(QVBoxLayout* col, const QString& title, ParamForm* form) {
    auto* g = new QGroupBox(title);
    auto* l = new QVBoxLayout(g);
    l->setContentsMargins(4, 8, 4, 4);
    if (form) {
        l->addWidget(form);
        forms_.push_back(form);
    }
    col->addWidget(g);
    return g;
}

ParamForm* MainWindow::buildObjectForm() {
    auto* f = new ParamForm(ctrl_.get());
    f->addCombo("Форма", kShapes, [](const Snap& s) { return int(s.obstacleSettings.shape); },
                [](Simulation& s, int v) {
                    if (ObstacleShape(v) == ObstacleShape::Custom && !s.obstacle.customMesh) return;
                    s.obstacle.shape = ObstacleShape(v);
                    s.rebuildObstacle();
                });
    f->addDouble("Размер", 0.02, 5, 0.05, 3, [](const Snap& s) { return s.obstacleSettings.size; },
                 [](Simulation& s, double v) { s.obstacle.size = float(v); s.rebuildObstacle(); },
                 "Характерный размер: диаметр / хорда / ребро / длина", "м");
    f->addDouble("Угол атаки", -90, 90, 1, 1, [](const Snap& s) { return s.obstacleSettings.angleOfAttackDeg; },
                 [](Simulation& s, double v) { s.obstacle.angleOfAttackDeg = float(v); s.rebuildObstacle(); },
                 "Поворот вокруг оси Z, нос вверх при положительном угле", "°");
    auto* imp = new QPushButton("Импорт OBJ / STL…");
    connect(imp, &QPushButton::clicked, this, &MainWindow::importMesh);
    f->addRow(imp);

    f->beginAdvanced();
    f->addDouble("Размах / длина", 0.05, 5, 0.05, 3, [](const Snap& s) { return s.obstacleSettings.span; },
                 [](Simulation& s, double v) { s.obstacle.span = float(v); s.rebuildObstacle(); }, "Размах крыла, длина цилиндра", "м");
    f->addDouble("Относит. толщина", 0.05, 1, 0.02, 2, [](const Snap& s) { return s.obstacleSettings.thickness; },
                 [](Simulation& s, double v) { s.obstacle.thickness = float(v); s.rebuildObstacle(); });
    f->addText("Профиль NACA", [](const Snap& s) { return QString::fromStdString(s.obstacleSettings.nacaCode); },
               [](Simulation& s, QString v) {
                   if (v.size() == 4) { s.obstacle.nacaCode = v.toStdString(); s.rebuildObstacle(); }
               },
               "Четырёхзначный код: 0012, 2412, 4415…");
    f->addDouble("Рыскание", -180, 180, 5, 1, [](const Snap& s) { return s.obstacleSettings.yawDeg; },
                 [](Simulation& s, double v) { s.obstacle.yawDeg = float(v); s.rebuildObstacle(); }, {}, "°");
    f->addDouble("Крен", -180, 180, 5, 1, [](const Snap& s) { return s.obstacleSettings.rollDeg; },
                 [](Simulation& s, double v) { s.obstacle.rollDeg = float(v); s.rebuildObstacle(); }, {}, "°");
    for (int a = 0; a < 3; ++a) {
        const char* names[3] = {"Позиция X", "Позиция Y", "Позиция Z"};
        f->addDouble(names[a], -5, 5, 0.05, 3, [a](const Snap& s) { return s.obstacleSettings.position[a]; },
                     [a](Simulation& s, double v) { s.obstacle.position[a] = float(v); s.rebuildObstacle(); }, {}, "м");
    }
    f->addHint("Изменение геометрии перезапускает расчёт.");
    return f;
}

ParamForm* MainWindow::buildFluidForm() {
    auto* f = new ParamForm(ctrl_.get());
    f->addDouble("Радиус частицы *", 3, 60, 1, 1, [](const Snap& s) { return s.particleParams.particleRadius * 1000; },
                 [](Simulation& s, double v) { s.particles.params.particleRadius = float(v / 1000); s.reset(); },
                 "Меньше радиус — больше частиц, точнее и медленнее", "мм");
    f->addDouble("Вязкость", 0, 1, 0.01, 3, [](const Snap& s) { return s.particleParams.viscosity; },
                 [](Simulation& s, double v) { s.particles.params.viscosity = float(v); }, "XSPH: 0 — вода, 0.3+ — мёд");
    f->addBool("Струя из источника", [](const Snap& s) { return s.emitter.enabled; },
               [](Simulation& s, bool v) { s.particles.emitter.enabled = v; });
    auto* addBlock = new QPushButton("Добавить объём жидкости");
    connect(addBlock, &QPushButton::clicked, this, [this] {
        ctrl_->post([](Simulation& s) {
            AABB d = s.particles.domain();
            Vector3 c = d.center(), e = d.extent();
            s.particles.addBlock(AABB({c.x - 0.15f * e.x, d.hi.y - 0.35f * e.y, c.z - 0.25f * e.z},
                                {c.x + 0.15f * e.x, d.hi.y - 0.05f * e.y, c.z + 0.25f * e.z}));
        });
    });
    f->addRow(addBlock);

    f->beginAdvanced();
    // The particle mass is calibrated from ρ0 when the scene is built: a new density needs a rebuild.
    f->addDouble("Плотность ρ0 *", 1, 20000, 50, 0, [](const Snap& s) { return s.particleParams.restDensity; },
                 [](Simulation& s, double v) { s.particles.params.restDensity = float(v); s.reset(); }, {}, "кг/м³");
    f->addInt("Итерации несжимаемости", 1, 30, [](const Snap& s) { return s.particleParams.solverIterations; },
              [](Simulation& s, int v) { s.particles.params.solverIterations = v; });
    f->addInt("Подшагов на кадр", 1, 20, [](const Snap& s) { return s.particleParams.substeps; },
              [](Simulation& s, int v) { s.particles.params.substeps = v; });
    f->addDouble("Завихренность", 0, 50, 0.5, 2, [](const Snap& s) { return s.particleParams.vorticity; },
                 [](Simulation& s, double v) { s.particles.params.vorticity = float(v); });
    f->addDouble("Поверхностное натяжение", 0, 0.01, 0.0001, 4, [](const Snap& s) { return s.particleParams.tensileK; },
                 [](Simulation& s, double v) { s.particles.params.tensileK = float(v); });
    f->addDouble("Трение о стенки", 0, 1, 0.05, 2, [](const Snap& s) { return s.particleParams.wallFriction; },
                 [](Simulation& s, double v) { s.particles.params.wallFriction = float(v); });
    f->addDouble("Гравитация g", -30, 30, 0.5, 2, [](const Snap& s) { return -s.rigid.gravity.y; },
                 [](Simulation& s, double v) { s.setGravity({0.0f, float(-v), 0.0f}); s.touchParams(); }, "Одна для всей сцены",
                 "м/с²");
    f->addDouble("Скорость струи", 0.1, 20, 0.5, 2, [](const Snap& s) { return s.emitter.speed; },
                 [](Simulation& s, double v) { s.particles.emitter.speed = float(v); }, {}, "м/с");
    f->addDouble("Радиус струи", 0.01, 0.3, 0.01, 3, [](const Snap& s) { return s.emitter.radius; },
                 [](Simulation& s, double v) { s.particles.emitter.radius = float(v); }, {}, "м");
    return f;
}

// The knobs of the loaded scene (Scene::params, e.g. the tokamak's safety factor): one row per
// knob, built again whenever another scene is loaded. Setting a knob rebuilds the scene
// (Simulation::setSceneParam resets it), so no reset() here.
ParamForm* MainWindow::buildSceneForm(const std::vector<SceneParam>& params) {
    auto* f = new ParamForm(ctrl_.get());
    for (size_t i = 0; i < params.size(); ++i) {
        const SceneParam& p = params[i];
        const QString label = QString::fromStdString(p.name) + " *";
        const QString tip = QString::fromStdString(p.tip);
        if (p.toggle)
            f->addBool(label, [i](const Snap& s) { return i < s.sceneParams.size() && s.sceneParams[i].value > 0.5f; },
                       [i](Simulation& s, bool v) { s.setSceneParam(int(i), v ? 1.0f : 0.0f); }, tip);
        else
            f->addDouble(label, p.min, p.max, p.step, p.decimals,
                         [i](const Snap& s) { return i < s.sceneParams.size() ? double(s.sceneParams[i].value) : 0.0; },
                         [i](Simulation& s, double v) { s.setSceneParam(int(i), float(v)); }, tip);
    }
    return f;
}

void MainWindow::rebuildSceneForm(const Snap& s) {
    if (sceneForm_) {
        forms_.erase(std::remove(forms_.begin(), forms_.end(), sceneForm_), forms_.end());
        sceneForm_->deleteLater();
    }
    sceneForm_ = buildSceneForm(s.sceneParams);
    sceneBox_->layout()->addWidget(sceneForm_);
    forms_.push_back(sceneForm_);
    sceneBox_->setVisible(!s.sceneParams.empty());
}

ParamForm* MainWindow::buildGasForm() {
    auto* f = new ParamForm(ctrl_.get());
    f->addDouble("Скорость потока U", 0, 100, 1, 2, [](const Snap& s) { return s.gasParams.inflowSpeed; },
                 [](Simulation& s, double v) { s.grid.params.inflowSpeed = float(v); },
                 "Скорость на входной границе (и масштаб для Cp/Cd)", "м/с");
    f->addInt("Ячеек по X *", 8, 400, [](const Snap& s) { return s.gasParams.resolutionX; },
              [](Simulation& s, int v) { s.grid.params.resolutionX = v; s.reset(); }, "Разрешение сетки вдоль X; по Y/Z — пропорционально");
    f->addBool("Источник тепла и дыма (сфера)", [](const Snap& s) { return s.heat.enabled; },
               [](Simulation& s, bool v) { s.grid.source.enabled = v; });
    f->addDouble("Радиус источника", 0.02, 2, 0.02, 2, [](const Snap& s) { return s.heat.radius; },
                 [](Simulation& s, double v) { s.grid.source.radius = float(v); }, {}, "м");
    f->addDouble("Подъёмная сила тепла", -50, 50, 0.5, 2, [](const Snap& s) { return s.gasParams.heatBuoyancy; },
                 [](Simulation& s, double v) { s.grid.params.heatBuoyancy = float(v); }, "Буссинеск: ускорение на единицу T", "м/с²");
    f->addBool("Трение о поверхность", [](const Snap& s) { return s.gasParams.wallFriction; },
               [](Simulation& s, bool v) { s.grid.params.wallFriction = v; },
               "Касательное трение газа о тела (пристеночная функция: Cf пограничного слоя по Шлихтингу)");
    f->addBool("Газ действует на тела", [](const Snap& s) { return s.gasPushesBodies; },
               [](Simulation& s, bool v) { s.gasPushesBodies = v; },
               "Давление газа и архимедова сила на твёрдые тела. Тела всегда вытесняют газ; "
               "с плотностью воздуха 1.2 кг/м³ обратное влияние слабое — увеличьте плотность среды");
    addGasExpertRows(f);
    return f;
}

// The gas form's rows under "Эксперт": the source's place and strength, dissipation, the medium,
// the box and its faces, and the pressure solver.
void MainWindow::addGasExpertRows(ParamForm* f) {
    f->beginAdvanced();
    for (int a = 0; a < 3; ++a) {
        const char* names[3] = {"Источник X", "Источник Y", "Источник Z"};
        f->addDouble(names[a], -20, 20, 0.05, 3, [a](const Snap& s) { return s.heat.center[a]; },
                     [a](Simulation& s, double v) { s.grid.source.center[a] = float(v); }, "Центр сферы-источника", "м");
    }
    // Smoke scenes: relative units (~1); fire: kelvin above the air (the burner is 400 K).
    f->addDouble("Температура источника", 0, 3000, 0.1, 2, [](const Snap& s) { return s.heat.temperature; },
                 [](Simulation& s, double v) { s.grid.source.temperature = float(v); },
                 "Дым: относительные единицы (~1); огонь: кельвины над температурой воздуха");
    f->addDouble("Плотность дыма источника", 0, 5, 0.1, 2, [](const Snap& s) { return s.heat.smoke; },
                 [](Simulation& s, double v) { s.grid.source.smoke = float(v); });
    f->addDouble("Вес дыма", -50, 50, 0.1, 2, [](const Snap& s) { return s.gasParams.smokeBuoyancy; },
                 [](Simulation& s, double v) { s.grid.params.smokeBuoyancy = float(v); }, {}, "м/с²");
    f->addDouble("Остывание газа", 0, 10, 0.05, 3, [](const Snap& s) { return s.gasParams.temperatureDissipation; },
                 [](Simulation& s, double v) { s.grid.params.temperatureDissipation = float(v); }, {}, "1/с");
    f->addDouble("Рассеяние дыма", 0, 5, 0.01, 3, [](const Snap& s) { return s.gasParams.smokeDissipation; },
                 [](Simulation& s, double v) { s.grid.params.smokeDissipation = float(v); }, {}, "1/с");
    f->addDouble("Сохранение вихрей", 0, 20, 0.1, 2, [](const Snap& s) { return s.gasParams.vorticityConfinement; },
                 [](Simulation& s, double v) { s.grid.params.vorticityConfinement = float(v); },
                 "Vorticity confinement: компенсирует численную диссипацию вихрей");
    f->addDouble("Плотность среды ρ", 0.01, 5000, 0.1, 3, [](const Snap& s) { return s.gasParams.fluidDensity; },
                 [](Simulation& s, double v) { s.grid.params.fluidDensity = float(v); }, "Воздух 1.225, вода 998", "кг/м³");
    f->addDouble("Кинем. вязкость ν", 0, 1, 1e-5, 6, [](const Snap& s) { return s.gasParams.kinematicViscosity; },
                 [](Simulation& s, double v) { s.grid.params.kinematicViscosity = float(v); }, "Воздух 1.5e-5 м²/с", "м²/с");
    for (int a = 0; a < 3; ++a) {
        const char* names[3] = {"Размер области X *", "Размер области Y *", "Размер области Z *"};
        f->addDouble(names[a], 0.2, 50, 0.1, 2, [a](const Snap& s) { return s.gasParams.domainSize[a]; },
                     [a](Simulation& s, double v) { s.grid.params.domainSize[a] = float(v); s.reset(); }, {}, "м");
    }
    const char* faces[6] = {"Граница −X *", "Граница +X *", "Граница −Y *", "Граница +Y *", "Граница −Z *", "Граница +Z *"};
    for (int i = 0; i < 6; ++i)
        f->addCombo(faces[i], kBC, [i](const Snap& s) { return int(s.gasParams.bc[i]); },
                    [i](Simulation& s, int v) { s.grid.params.bc[i] = BoundaryType(v); s.reset(); });
    f->addDouble("Число Куранта (CFL)", 0.2, 8, 0.1, 2, [](const Snap& s) { return s.gasParams.cfl; },
                 [](Simulation& s, double v) { s.grid.params.cfl = float(v); });
    f->addInt("Макс. итераций давления", 10, 5000, [](const Snap& s) { return s.gasParams.maxPressureIterations; },
              [](Simulation& s, int v) { s.grid.params.maxPressureIterations = v; });
    f->addDouble("Точность давления", 1e-7, 1e-1, 1e-5, 7, [](const Snap& s) { return s.gasParams.pressureTolerance; },
                 [](Simulation& s, double v) { s.grid.params.pressureTolerance = float(v); }, "Относительная невязка PCG");
    f->addBool("Адвекция MacCormack (2-й порядок)", [](const Snap& s) { return s.gasParams.maccormack; },
               [](Simulation& s, bool v) { s.grid.params.maccormack = v; });
    f->addBool("Дымовые струйки на входе", [](const Snap& s) { return s.gasParams.smokeRake; },
               [](Simulation& s, bool v) { s.grid.params.smokeRake = v; });
    f->addBool("Cd/Cl по площади в плане", [](const Snap& s) { return s.gasParams.usePlanformArea; },
               [](Simulation& s, bool v) { s.grid.params.usePlanformArea = v; }, "Иначе — по площади миделя");
}

ParamForm* MainWindow::buildBrushForm() {
    auto* f = new ParamForm(ctrl_.get());
    auto* radius = new QDoubleSpinBox;
    radius->setRange(0.02, 2.0);
    radius->setSingleStep(0.01);
    radius->setValue(brush_.radius);
    radius->setSuffix(" м");
    connect(radius, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        brush_.radius = float(v);
        view_->setBrushRadius(float(v));
    });
    f->addRow("Радиус", radius);
    auto* strength = new QDoubleSpinBox;
    strength->setRange(0, 10);
    strength->setSingleStep(0.1);
    strength->setValue(brush_.strength);
    strength->setToolTip("Скорость газа = скорость движения точки под курсором × сила");
    connect(strength, &QDoubleSpinBox::valueChanged, this, [this](double v) { brush_.strength = float(v); });
    f->addRow("Сила", strength);
    auto* smoke = new QDoubleSpinBox;
    smoke->setRange(0, 1);
    smoke->setSingleStep(0.05);
    smoke->setValue(brush_.smoke);
    connect(smoke, &QDoubleSpinBox::valueChanged, this, [this](double v) { brush_.smoke = float(v); });
    f->addRow("Дым", smoke);
    f->beginAdvanced();
    auto* heat = new QDoubleSpinBox;
    heat->setRange(0, 5);
    heat->setSingleStep(0.1);
    heat->setValue(brush_.heat);
    connect(heat, &QDoubleSpinBox::valueChanged, this, [this](double v) { brush_.heat = float(v); });
    f->addRow("Тепло", heat);
    f->addHint("ЛКМ по газу: луч из курсора пересекается с плоскостью сечения; движение точки "
               "пересечения задаёт скорость газа. Камера — зажатое колесо.");
    return f;
}

// Region where the body buttons spawn / throw bodies in the current mode.
static AABB bodyArea(const Simulation& s) {
    if (s.mode() == SimMode::Fluid) return s.particles.domain();
    if (s.mode() == SimMode::WindTunnel) return s.grid.domain();
    return AABB({-2, 0, -2}, {2, 5, 2});
}

// Where the body buttons drop a new body: high in the body area, a little to one side (a fixed
// seed: the same sequence every run).
static Vector3 bodySpawnPos(Simulation& s) {
    static std::mt19937 rng(42);
    std::uniform_real_distribution<float> U(-0.3f, 0.3f);
    AABB d = bodyArea(s);
    Vector3 c = d.center(), e = d.extent();
    return Vector3(c.x + U(rng) * e.x, d.hi.y - 0.15f * e.y, c.z + U(rng) * e.z);
}

// Where the soft-body and cloth buttons drop theirs: near the top of the particles' box.
static Vector3 particleSpawnTop(Simulation& s) {
    static std::mt19937 rng(7);
    std::uniform_real_distribution<float> U(-0.3f, 0.3f);
    AABB d = s.particles.domain();
    Vector3 c = d.center(), e = d.extent();
    return Vector3(c.x + U(rng) * e.x, d.hi.y - 0.2f * e.y, c.z + 0.3f * U(rng) * e.z);
}

// A row of buttons in the form, and a button in a row that runs `job` on the simulation's thread.
static QHBoxLayout* buttonRow(ParamForm* f) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    f->addRow(row);
    return h;
}

void MainWindow::addJobButton(QHBoxLayout* row, const QString& text, const QString& tip, std::function<void(Simulation&)> job) {
    auto* b = new QPushButton(text);
    if (!tip.isEmpty()) b->setToolTip(tip);
    row->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, job] { ctrl_->post(job); });
}

ParamForm* MainWindow::buildRigidForm() {
    auto* f = new ParamForm(ctrl_.get());
    addBodyButtons(f);
    addSoftButtons(f);
    f->beginAdvanced();
    addRigidSolverRows(f);
    return f;
}

// Rigid bodies: a ball, a box, a convex hull, a bullet for the CCD, a throw; the concave teapot and bunny.
void MainWindow::addBodyButtons(ParamForm* f) {
    QHBoxLayout* h = buttonRow(f);
    addJobButton(h, "+ Шар", {}, [](Simulation& s) {
        s.rigid.addSphere(bodySpawnPos(s), s.mode() == SimMode::Rigid ? 0.2f : s.mode() == SimMode::Fluid ? 0.08f : 0.12f, 500.0f, {0.9f, 0.35f, 0.3f});
    });
    addJobButton(h, "+ Куб", {}, [](Simulation& s) {
        float hh = s.mode() == SimMode::Rigid ? 0.18f : s.mode() == SimMode::Fluid ? 0.07f : 0.11f;
        s.rigid.addBox(bodySpawnPos(s), Vector3(hh), Quaternion::fromAxisAngle({1, 1, 0.3f}, 0.8f), 500.0f, {0.3f, 0.7f, 0.9f});
    });
    addJobButton(h, "+ Многогранник", "Выпуклый многогранник (цилиндр/конус/гранёный шар по очереди); столкновения через GJK-EPA",
                 [](Simulation& s) {
                     static int k = 0;
                     const int kind = k++ % 3; // cylinder, cone, faceted ball in turn
                     float sc = s.mode() == SimMode::Rigid ? 1.0f : s.mode() == SimMode::Fluid ? 0.5f : 0.7f;
                     TriMesh m = kind == 0   ? primitives::cylinder(0.14f * sc, 0.3f * sc, 12)
                                 : kind == 1 ? primitives::cone(0.16f * sc, 0.35f * sc, 12)
                                             : primitives::sphere(0.16f * sc, 8, 5);
                     s.rigid.addConvex(m, bodySpawnPos(s), Quaternion::fromAxisAngle({1, 0.3f, 0.2f}, 0.9f), 600.0f, {0.8f, 0.8f, 0.3f});
                 });
    addJobButton(h, "Пуля", "Маленький шар на 150 м/с — проверка CCD", [](Simulation& s) {
        AABB d = bodyArea(s);
        Vector3 e = d.extent();
        int i = s.rigid.addSphere({d.lo.x + 0.05f * e.x, d.lo.y + 0.3f * e.y, d.center().z}, 0.03f, 8000.0f, {1.0f, 0.95f, 0.4f});
        s.rigid.bodies()[i].vel = {150.0f, 0.0f, 0.0f};
    });
    addJobButton(h, "Бросить", {}, [](Simulation& s) {
        AABB d = bodyArea(s);
        Vector3 e = d.extent();
        int i = s.rigid.addSphere({d.lo.x + 0.1f * e.x, d.lo.y + 0.7f * e.y, d.center().z},
                                  s.mode() == SimMode::Rigid ? 0.22f : s.mode() == SimMode::Fluid ? 0.07f : 0.12f, 3000.0f, {0.95f, 0.8f, 0.2f});
        s.rigid.bodies()[i].vel = Vector3(1.8f * e.x, 0.3f * e.y, 0.0f);
    });
    QHBoxLayout* h2 = buttonRow(f);
    addJobButton(h2, "+ Чайник", "Невыпуклое тело: чайник, разбитый на выпуклые части", [](Simulation& s) {
        s.rigid.addCompound(teapotShape(), bodySpawnPos(s), Quaternion::fromAxisAngle({0.3f, 1, 0.2f}, 0.7f), 500.0f, {0.8f, 0.55f, 0.85f});
    });
    addJobButton(h2, "+ Кролик", "Невыпуклое тело: кролик, разбитый на выпуклые части", [](Simulation& s) {
        s.rigid.addCompound(bunnyShape(), bodySpawnPos(s), Quaternion::fromAxisAngle({0, 1, 0}, 0.5f), 500.0f, {0.92f, 0.9f, 0.86f});
    });
}

// Soft bodies and cloth: the particle system runs in every mode (liquid, gas, rigid).
void MainWindow::addSoftButtons(ParamForm* f) {
    QHBoxLayout* sh = buttonRow(f);
    addJobButton(sh, "+ Вода", "Объём воды (частицы PBF) сверху: в режиме газа она связана с воздухом", [](Simulation& s) {
        AABB d = s.particles.domain();
        Vector3 c = d.center(), e = d.extent();
        s.particles.addBlock(AABB({c.x - 0.12f * e.x, d.hi.y - 0.4f * e.y, c.z - 0.25f * e.z},
                                  {c.x + 0.12f * e.x, d.hi.y - 0.1f * e.y, c.z + 0.25f * e.z}));
    });
    addJobButton(sh, "+ Мягкий куб", "Поролон 150 кг/м³: частицы, форма держится сопоставлением формы (Müller 2005)", [](Simulation& s) {
        TriMesh m = primitives::box(Vector3(0.08f));
        m.translate(particleSpawnTop(s));
        s.particles.addSoftBody(m, 150.0f, 0.4f, {0.3f, 0.75f, 0.95f});
    });
    addJobButton(sh, "+ Желе-шар", "Мягкий шар: низкая жёсткость формы", [](Simulation& s) {
        TriMesh m = primitives::sphere(0.08f, 16, 8);
        m.translate(particleSpawnTop(s));
        s.particles.addSoftBody(m, 150.0f, 0.15f, {0.55f, 0.9f, 0.35f});
    });
    addJobButton(sh, "+ Ткань", "Свободный лоскут хлопка 0.3 кг/м² (XPBD): падает, ложится на всё, рвётся по нитям", [](Simulation& s) {
        Vector3 p = particleSpawnTop(s);
        ClothMaterial cotton; // defaults: cotton, tears at ~4 kN/m
        s.particles.addCloth(p - Vector3(0.2f, 0, 0.2f), {0.4f, 0, 0}, {0, 0, 0.4f}, cotton, 0, {0.9f, 0.85f, 0.75f});
    });
}

// The rigid solver's rows under "Эксперт".
void MainWindow::addRigidSolverRows(ParamForm* f) {
    f->addCombo("Решатель", {"XPBD (Müller 2020, эксперим.)", "Импульсы + ударная волна"},
                [](const Snap& s) { return int(s.rigid.solver); },
                [](Simulation& s, int v) {
                    s.rigid.params.solver = RigidSolver(v);
                    s.rigid.params.substeps = v == 0 ? 30 : 10;
                    s.touchParams();
                },
                "Импульсный: тёплый старт, split impulse, ударная волна, трение манифолда и замок SO(3)");
    f->addBool("Ударная волна (стопки)", [](const Snap& s) { return s.rigid.shockPropagation; },
               [](Simulation& s, bool v) { s.rigid.params.shockPropagation = v; });
    f->addBool("Замок вращения SO(3)", [](const Snap& s) { return s.rigid.rotationalLock; },
               [](Simulation& s, bool v) { s.rigid.params.rotationalLock = v; });
    f->addBool("CCD — непрерывные столкновения", [](const Snap& s) { return s.rigid.ccd; },
               [](Simulation& s, bool v) { s.rigid.params.ccd = v; },
               "Консервативное продвижение на GJK: быстрые тела не пролетают сквозь тонкие стенки");
    f->addBool("Блочный решатель манифолда (LCP)", [](const Snap& s) { return s.rigid.blockSolver; },
               [](Simulation& s, bool v) { s.rigid.params.blockSolver = v; });
    f->addBool("Засыпание островов тел", [](const Snap& s) { return s.rigid.sleeping; },
               [](Simulation& s, bool v) {
                   s.rigid.params.sleeping = v;
                   for (int i = 0; i < int(s.rigid.bodies().size()); ++i) s.rigid.wake(i);
               },
               "Покоящиеся острова (граф контактов) перестают считаться до касания; рисуются темнее");
    f->addInt("Итерации контактов", 1, 100, [](const Snap& s) { return s.rigid.iterations; },
              [](Simulation& s, int v) { s.rigid.params.iterations = v; });
    f->addInt("Подшагов", 1, 60, [](const Snap& s) { return s.rigid.substeps; }, // XPBD uses 30
              [](Simulation& s, int v) { s.rigid.params.substeps = v; });
    f->addDouble("Лин. демпфирование", 0, 5, 0.01, 3, [](const Snap& s) { return s.rigid.linearDamping; },
                 [](Simulation& s, double v) { s.rigid.params.linearDamping = float(v); }, {}, "1/с");
    f->addDouble("Угл. демпфирование", 0, 5, 0.01, 3, [](const Snap& s) { return s.rigid.angularDamping; },
                 [](Simulation& s, double v) { s.rigid.params.angularDamping = float(v); }, {}, "1/с");
    // One gravity for bodies, particles and the flame (Simulation::setGravity); the same row as in
    // the liquid form - touchParams refreshes both.
    f->addDouble("Гравитация g", -30, 30, 0.5, 2, [](const Snap& s) { return -s.rigid.gravity.y; },
                 [](Simulation& s, double v) { s.setGravity({0.0f, float(-v), 0.0f}); s.touchParams(); }, "Одна для всей сцены",
                 "м/с²");
}

void MainWindow::buildParameterDock() {
    auto* dock = new QDockWidget("Параметры", this);
    dock->setObjectName("paramsDock");
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget;
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(6, 6, 6, 6);
    col->setSpacing(8);

    gasBox_ = addGroup(col, "Газ / поток — сетка Навье–Стокса", buildGasForm());
    sceneBox_ = addGroup(col, "Сцена", nullptr); // filled by rebuildSceneForm() for the loaded scene
    brushBox_ = addGroup(col, "Кисть возмущения", buildBrushForm());
    fluidBox_ = addGroup(col, "Жидкость — частицы SPH", buildFluidForm());
    objectBox_ = addGroup(col, "Объект / препятствие", buildObjectForm());
    rigidBox_ = addGroup(col, "Твёрдые тела", buildRigidForm());
    col->addStretch(1);

    scroll->setWidget(host);
    scroll->setMinimumWidth(340);
    dock->setWidget(scroll);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    menuBar()->findChild<QMenu*>("viewMenu")->addAction(dock->toggleViewAction());
}

// ---------------------------------------------------------------------------
// Scene builder ("Конструктор"): shapes with roles, saved as *.rfscene
// ---------------------------------------------------------------------------
// One panel on the right with two tabs, like Unity's Hierarchy and Inspector: "Сцена" (the list of
// objects and the world) and "Объект" (the selected object: its card, its geometry, its components).
// Selecting an object turns to "Объект". The visualisation settings share the place as a tab.
static QScrollArea* scrolled(QWidget* content) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // the fields shrink, the panel never scrolls sideways
    scroll->setWidget(content);
    scroll->setMinimumWidth(340);
    return scroll;
}

void MainWindow::buildSceneBuilderDock() {
    builder_ = new SceneBuilder(ctrl_.get(), this);
    inspectorTabs_ = new QTabWidget;
    inspectorTabs_->setObjectName("inspectorTabs");
    inspectorTabs_->addTab(scrolled(builder_->sceneListPanel()), "Сцена");
    inspectorTabs_->addTab(scrolled(builder_->inspectorPanel()), "Объект");
    inspectorTabs_->setTabToolTip(0, "Все объекты сцены и мир: воздух, гравитация");
    inspectorTabs_->setTabToolTip(1, "Выбранный объект: где стоит, как выглядит и что умеет");
    auto* dock = new QDockWidget("Инспектор", this);
    dock->setObjectName("inspectorDock");
    dock->setWidget(inspectorTabs_);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    menuBar()->findChild<QMenu*>("viewMenu")->addAction(dock->toggleViewAction());
    if (auto* vis = findChild<QDockWidget*>("visDock")) {
        tabifyDockWidget(dock, vis);
        dock->raise();
    }
}

void MainWindow::connectSceneBuilder() {
    connect(builder_, &SceneBuilder::statusMessage, this, [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    connect(builder_, &SceneBuilder::modeChanged, this, &MainWindow::onModeChanged);
    connect(builder_, &SceneBuilder::editSnapshot, this, [this](std::shared_ptr<const rf::RenderSnapshot> s) {
        if (builder_->editing()) view_->setSnapshot(std::move(s));
    });
    connect(builder_, &SceneBuilder::highlightBodies, view_, &Viewport::setHighlightBodies);
    connect(builder_, &SceneBuilder::hoverBodies, view_, &Viewport::setHoverBodies);
    connect(builder_, &SceneBuilder::ghostBodies, view_, &Viewport::setGhostBodies);
    connect(builder_, &SceneBuilder::unpickableBodies, view_, &Viewport::setUnpickableBodies);
    connect(builder_, &SceneBuilder::gizmoTarget, view_, &Viewport::setGizmoTarget);
    connect(builder_, &SceneBuilder::sceneryBodies, view_, &Viewport::setSceneryBodies);
    connect(builder_, &SceneBuilder::frameRequested, this, [this](const rf::AABB& box) {
        view_->setFocusBox(box);
        if (!view_->lookingThrough()) view_->frameScene(); // through a camera, framing would move the camera
    });
    connect(builder_, &SceneBuilder::sceneMarkers, view_, &Viewport::setSceneMarkers);
    connect(builder_, &SceneBuilder::cameraView, view_, &Viewport::setLookThrough);
    connect(view_, &Viewport::shadowsLeftOut, this, &MainWindow::showShadowBudget);
    connect(view_, &Viewport::lookThroughMoved, builder_, &SceneBuilder::onViewMovedThroughCamera);
    builder_->setViewEye([this] {
        const QVector3D e = view_->camera().eye();
        return rf::Vector3(e.x(), e.y(), e.z());
    });
    connect(view_, &Viewport::bodyClicked, builder_, &SceneBuilder::selectBody);
    connect(view_, &Viewport::entityClicked, builder_, &SceneBuilder::onEntityClicked);
    connect(view_, &Viewport::entityHovered, builder_, &SceneBuilder::onEntityHovered);
    connect(view_, &Viewport::gizmoStarted, builder_, &SceneBuilder::onGizmoStarted);
    connect(view_, &Viewport::gizmoMoved, builder_, &SceneBuilder::onGizmoMoved);
    connect(view_, &Viewport::gizmoFinished, builder_, &SceneBuilder::onGizmoFinished);
    connect(view_, &Viewport::gizmoCancelled, builder_, &SceneBuilder::onGizmoCancelled);
    connect(view_, &Viewport::cloneDragStarted, builder_, &SceneBuilder::onCloneDragStarted);
    connect(view_, &Viewport::cloneDragFinished, this, &MainWindow::showClonePopover);
    connect(view_, &Viewport::editContextMenu, this, &MainWindow::showEditContextMenu);
    // What the mouse ray hits: the authored shapes in edit mode, the simulated bodies on pause.
    view_->setEditPicker([this](const Ray& ray) {
        if (builder_->editing()) return builder_->pickAll(ray);
        std::vector<PickHit> hits;
        int body;
        rf::Vector3 hit;
        if (view_->pickAnyBody(ray, body, hit) && builder_->clickTarget(builder_->entityOfBody(body)))
            hits.push_back({builder_->clickTarget(builder_->entityOfBody(body)), rf::length(hit - ray.origin), hit, ""});
        return hits;
    });
    connect(builder_, &SceneBuilder::selectionChanged, this, [this](uint32_t id) {
        if (id != 0) inspectorTabs_->setCurrentIndex(1); // selecting shows the object
    });
    connect(builder_, &SceneBuilder::colliderGuides, view_, &Viewport::setColliderGuides);
}

void MainWindow::buildSceneMenu() {
    auto* menu = new QMenu("С&цена", this);
    menu->addAction("Новая пустая сцена", QKeySequence::New, this, [this] { builder_->newScene(); });
    menu->addAction("Открыть…", QKeySequence::Open, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Открыть сцену", examplesDir(), "Сцена PhysRealFlow (*.rfscene)");
        if (path.isEmpty()) return;
        QString error;
        if (!builder_->openFile(path, error)) QMessageBox::warning(this, "Открыть сцену", error);
    });
    menu->addAction("Сохранить…", QKeySequence::Save, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, "Сохранить сцену", examplesDir(), "Сцена PhysRealFlow (*.rfscene)");
        if (path.isEmpty()) return;
        QString error;
        if (!builder_->saveFile(path, error)) QMessageBox::warning(this, "Сохранить сцену", error);
    });
    menu->addSeparator();
    menu->addAction("Примеры…", this, &MainWindow::openGallery);
    menu->addMenu(samplesMenu_);
    menu->addSeparator();
    // TODO: the node view - entities and roles as boxes wired together (the next step of the builder).
    auto* graphView = menu->addAction("Граф ролей (скоро)");
    graphView->setEnabled(false);
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    menuBar()->insertMenu(viewMenu->menuAction(), menu);
}

// ---------------------------------------------------------------------------
// Visualisation dock
// ---------------------------------------------------------------------------
void MainWindow::buildVisualDock() {
    auto* dock = new QDockWidget("Визуализация", this);
    dock->setObjectName("visDock");
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget;
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(6, 6, 6, 6);

    buildFieldView(col);
    buildParticleView(col);
    buildDisplayView(col);
    col->addStretch(1);
    scroll->setWidget(host);
    scroll->setMinimumWidth(290);
    dock->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    menuBar()->findChild<QMenu*>("viewMenu")->addAction(dock->toggleViewAction());
}

// The flow field: which field, the slice, smoke, field lines, the voxel grid and the velocity
// arrows; under "Эксперт" the arrows' density and length, streamlines, Cp, the colour scale.
void MainWindow::buildFieldView(QVBoxLayout* col) {
    auto* f = new ParamForm(ctrl_.get());
    f->addCombo("Поле", kFields, [](const Snap& s) { return int(s.vis.sliceField); },
                [](Simulation& s, int v) { s.vis.sliceField = GridField(v); });
    f->addCombo("Сечение ⟂ оси", {"X", "Y", "Z"}, [](const Snap& s) { return s.vis.sliceAxis; },
                [](Simulation& s, int v) { s.vis.sliceAxis = v; });
    f->addSlider("Положение", 0, 1, 200, [](const Snap& s) { return s.vis.slicePosition; },
                 [](Simulation& s, double v) { s.vis.slicePosition = float(v); });
    f->addBool("Цветное сечение поля", [](const Snap& s) { return s.vis.showSlice; },
               [](Simulation& s, bool v) { s.vis.showSlice = v; });
    f->addBool("Дым (объёмный рендер)", [](const Snap& s) { return s.vis.showSmoke; },
               [](Simulation& s, bool v) { s.vis.showSmoke = v; });
    f->addBool("Силовые линии магнитного поля", [](const Snap& s) { return s.vis.showFieldLines; },
               [](Simulation& s, bool v) { s.vis.showFieldLines = v; });
    f->addCombo("Сетка вокселей", {"Скрыть", "В плоскости сечения", "Вся 3D-сетка"},
                [](const Snap& s) { return s.vis.gridDisplay; }, [](Simulation& s, int v) { s.vis.gridDisplay = v; },
                "Границы ячеек расчётной сетки");
    f->addCombo("Векторы скорости", {"Скрыть", "В плоскости сечения", "Во всём объёме"},
                [](const Snap& s) { return s.vis.vectorDisplay; }, [](Simulation& s, int v) { s.vis.vectorDisplay = v; },
                "Стрелка в центре ячейки: скорость, усреднённая по граням MAC-ячейки");

    f->beginAdvanced();
    f->addInt("Прореживание векторов", 1, 16, [](const Snap& s) { return s.vis.vectorStride; },
              [](Simulation& s, int v) { s.vis.vectorStride = v; }, "Каждая n-я ячейка", "яч.");
    f->addDouble("Длина векторов", 0.1, 10, 0.1, 2, [](const Snap& s) { return s.vis.vectorScale; },
                 [](Simulation& s, double v) { s.vis.vectorScale = float(v); });
    f->addBool("Векторы только в дыму", [](const Snap& s) { return s.vis.vectorsWhereSmoke; },
               [](Simulation& s, bool v) { s.vis.vectorsWhereSmoke = v; });
    f->addBool("Линии тока", [](const Snap& s) { return s.vis.showStreamlines; },
               [](Simulation& s, bool v) { s.vis.showStreamlines = v; });
    f->addInt("Густота линий тока", 2, 40, [](const Snap& s) { return s.vis.streamlineSeeds; },
              [](Simulation& s, int v) { s.vis.streamlineSeeds = v; });
    f->addBool("Cp на поверхности тела", [](const Snap& s) { return s.vis.surfacePressure; },
               [](Simulation& s, bool v) { s.vis.surfacePressure = v; });
    f->addBool("Автодиапазон шкалы", [](const Snap& s) { return s.vis.autoRange; },
               [](Simulation& s, bool v) { s.vis.autoRange = v; });
    f->addDouble("Минимум шкалы", -1e6, 1e6, 0.1, 3, [](const Snap& s) { return s.vis.rangeMin; },
                 [](Simulation& s, double v) { s.vis.rangeMin = float(v); s.vis.autoRange = false; s.touchParams(); });
    f->addDouble("Максимум шкалы", -1e6, 1e6, 0.1, 3, [](const Snap& s) { return s.vis.rangeMax; },
                 [](Simulation& s, double v) { s.vis.rangeMax = float(v); s.vis.autoRange = false; s.touchParams(); });
    auto* smoke = new QDoubleSpinBox;
    smoke->setRange(0.5, 100);
    smoke->setValue(16);
    connect(smoke, &QDoubleSpinBox::valueChanged, view_, [this](double v) { view_->setSmokeDensity(float(v)); });
    f->addRow("Плотность дыма (рендер)", smoke);
    auto* alpha = new QDoubleSpinBox;
    alpha->setRange(0.1, 1);
    alpha->setSingleStep(0.05);
    alpha->setValue(0.92);
    connect(alpha, &QDoubleSpinBox::valueChanged, view_, [this](double v) { view_->setSliceOpacity(float(v)); });
    f->addRow("Непрозрачность сечения", alpha);
    fieldBox_ = addGroup(col, "Поле течения", f);
}

// The particles: their colour, and under "Эксперт" their drawn size.
void MainWindow::buildParticleView(QVBoxLayout* col) {
    auto* f = new ParamForm(ctrl_.get());
    f->addCombo("Цвет частиц", {"Скорость", "Плотность", "Однотонный"},
                [](const Snap& s) { return int(s.vis.particleColoring); },
                [](Simulation& s, int v) { s.vis.particleColoring = ParticleColoring(v); });
    f->beginAdvanced();
    auto* pscale = new QDoubleSpinBox;
    pscale->setRange(0.2, 3.0);
    pscale->setSingleStep(0.1);
    pscale->setValue(1.0);
    connect(pscale, &QDoubleSpinBox::valueChanged, view_, [this](double v) { view_->setParticleScale(float(v)); });
    f->addRow("Размер частиц", pscale);
    particleBox_ = addGroup(col, "Частицы", f);
}

// The display: the palette, convex parts, the water surface; under "Эксперт" the box, the floor
// grid and the engine's debug drawing.
void MainWindow::buildDisplayView(QVBoxLayout* col) {
    auto* f = new ParamForm(ctrl_.get());
    auto* cmap = new QComboBox;
    cmap->addItems({"Turbo", "Viridis", "Холодный–тёплый", "Оттенки серого"});
    connect(cmap, &QComboBox::currentIndexChanged, view_, &Viewport::setColormap);
    f->addRow("Палитра", cmap);
    auto* parts = new QCheckBox("Выпуклые части тел");
    parts->setToolTip("Показывать невыпуклые тела как набор выпуклых оболочек, с которыми работает коллизия");
    connect(parts, &QCheckBox::toggled, view_, &Viewport::setShowConvexParts);
    f->addRow(parts);
    // Liquid exists in every mode (pool scenes, water in the gas): the switch is always shown.
    f->addBool("Поверхность воды (шейдер)", [](const Snap& s) { return s.vis.liquidSurface; },
               [](Simulation& s, bool v) { s.vis.liquidSurface = v; });
    f->beginAdvanced();
    auto* dom = new QCheckBox("Границы области");
    dom->setChecked(true);
    connect(dom, &QCheckBox::toggled, view_, &Viewport::setShowDomain);
    f->addRow(dom);
    auto* floor = new QCheckBox("Сетка пола");
    floor->setChecked(true);
    connect(floor, &QCheckBox::toggled, view_, &Viewport::setShowFloor);
    f->addRow(floor);
    // The engine's debug drawing (Probe::line / point / box from any solver): contact points
    // and normals, body bounds ... Off, it costs the solvers one flag test.
    auto* dbg = new QCheckBox("Отладочная отрисовка (Probe)");
    dbg->setToolTip("Точки и нормали контактов, границы тел и всё, что решатели рисуют через rf::Probe");
    connect(dbg, &QCheckBox::toggled, this, [](bool on) { Probe::enableDraw(on); });
    f->addRow(dbg);
    addGroup(col, "Отображение", f);
}

// ---------------------------------------------------------------------------
// Results dock: numbers on the left, one chart per quantity on the right
// ---------------------------------------------------------------------------
void MainWindow::buildResultsDock() {
    auto* dock = new QDockWidget("Результаты", this);
    dock->setObjectName("resultsDock");

    info_ = new QTableWidget(0, 2);
    info_->setHorizontalHeaderLabels({"Величина", "Значение"});
    info_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    info_->horizontalHeader()->setStretchLastSection(true);
    info_->verticalHeader()->setVisible(false);
    info_->verticalHeader()->setDefaultSectionSize(22);
    info_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    info_->setSelectionMode(QAbstractItemView::NoSelection);
    info_->setShowGrid(false);
    info_->setAlternatingRowColors(true);

    plots_ = new PlotPanel;
    auto* plotHost = new QWidget;
    auto* pl = new QVBoxLayout(plotHost);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(plots_, 1);
    auto* bar = new QHBoxLayout;
    bar->addStretch(1);
    auto* clr = new QPushButton("Очистить");
    connect(clr, &QPushButton::clicked, plots_, &PlotPanel::clear);
    auto* csv = new QPushButton("Экспорт CSV…");
    connect(csv, &QPushButton::clicked, this, &MainWindow::exportCsv);
    bar->addWidget(plots_->channelButton());
    bar->addWidget(clr);
    bar->addWidget(csv);
    pl->addLayout(bar);

    // The probe's channels: every quantity the engine reported this frame, by name.
    sensors_ = new QTableWidget(0, 2);
    sensors_->setHorizontalHeaderLabels({"Датчик (Probe)", "Значение"});
    sensors_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    sensors_->horizontalHeader()->setStretchLastSection(true);
    sensors_->verticalHeader()->setVisible(false);
    sensors_->verticalHeader()->setDefaultSectionSize(20);
    sensors_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sensors_->setSelectionMode(QAbstractItemView::NoSelection);
    sensors_->setShowGrid(false);
    sensors_->setAlternatingRowColors(true);
    sensors_->setToolTip("Всё, что решатели сообщили отладчику rf::Probe: значения, счётчики за кадр, таймеры (мс)");
    auto* left = new QSplitter(Qt::Vertical);
    left->addWidget(info_);
    left->addWidget(sensors_);
    left->setSizes({260, 200});

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(left);
    split->addWidget(plotHost);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({340, 1000});
    dock->setWidget(split);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    menuBar()->findChild<QMenu*>("viewMenu")->addAction(dock->toggleViewAction());
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
void MainWindow::onDisturbance(Vector3 pos, Vector3 vel) {
    Disturbance d;
    d.center = pos;
    d.radius = brush_.radius;
    d.velocity = vel * brush_.strength;
    d.velocityBlend = rf::length(vel) > 1e-6f ? 1.0f : 0.0f; // a click adds smoke without stopping the flow
    d.smoke = brush_.smoke;
    d.heat = brush_.heat;
    ctrl_->post([d](Simulation& s) {
        if (s.mode() == SimMode::WindTunnel) s.grid.applyDisturbance(d);
    });
    if (!ctrl_->isRunning()) ctrl_->requestStep(); // make the effect visible even when paused
}

void MainWindow::poll() {
    uint64_t serial = ctrl_->serial();
    if (serial == lastSerial_) return;
    lastSerial_ = serial;
    if (auto s = ctrl_->snapshot()) onSnapshot(s);
}

void MainWindow::updateModeVisibility(SimMode m) {
    gasBox_->setVisible(m == SimMode::WindTunnel);
    brushBox_->setVisible(m == SimMode::WindTunnel);
    fluidBox_->setVisible(m == SimMode::Fluid);
    rigidBox_->setVisible(true); // bodies can be added in every mode (fluid, gas, rigid)
    fieldBox_->setVisible(m == SimMode::WindTunnel);
    particleBox_->setVisible(m == SimMode::Fluid);
}

void MainWindow::updateInfo(const Snap& s) {
    info_->setRowCount(int(s.info.size()));
    for (int r = 0; r < int(s.info.size()); ++r)
        for (int c = 0; c < 2; ++c) {
            QTableWidgetItem* it = info_->item(r, c);
            if (!it) info_->setItem(r, c, it = new QTableWidgetItem);
            it->setText(QString::fromStdString(c == 0 ? s.info[r].first : s.info[r].second));
        }
    // With the graphs folded away, the status bar carries the three main readings of the scene.
    QString readings;
    int shown = 0;
    for (const auto& [name, value] : s.info) {
        if (!expertAct_->isChecked() || graphsAct_->isChecked() || shown == 3 || name == "Время") continue;
        readings += QString("  ·  %1 %2").arg(QString::fromStdString(name), QString::fromStdString(value));
        ++shown;
    }
    status_->setText(QString("%1  ·  кадр %2  ·  t = %3 с  ·  %4%5")
                         .arg(QString::fromStdString(s.sceneName))
                         .arg(s.frame)
                         .arg(s.time, 0, 'f', 3)
                         .arg(modeText(), readings));
    updateSensors(s);
}

void MainWindow::updateSensors(const Snap& s) {
    const auto& ch = s.probe.channels;
    sensors_->setRowCount(int(ch.size()));
    for (int r = 0; r < int(ch.size()); ++r) {
        const Probe::Channel& c = ch[r];
        QString value = QString::number(c.value, 'g', 4);
        if (c.kind == Probe::Kind::TimerMs) value += " мс";
        else if (c.kind == Probe::Kind::Counter && c.value == std::floor(c.value)) value = QString::number(qint64(c.value));
        for (int col = 0; col < 2; ++col) {
            QTableWidgetItem* it = sensors_->item(r, col);
            if (!it) sensors_->setItem(r, col, it = new QTableWidgetItem);
            it->setText(col == 0 ? QString::fromStdString(c.name) : value);
        }
    }
}

void MainWindow::onSnapshot(const std::shared_ptr<const Snap>& s) {
    if (builder_->editing()) return; // edit mode shows the authored scene (SceneBuilder::editSnapshot), not the simulation
    builder_->onSimulationSnapshot(*s);
    // A ready-made scene is framed whole again; the builder's scenes frame their objects.
    if (s->sceneName != lastSceneName_ && s->sceneName.rfind(SceneBuilder::sceneName(), 0) != 0) view_->setFocusBox(rf::AABB());
    view_->setSnapshot(s);

    const bool sceneChanged = s->sceneName != lastSceneName_;
    if (sceneChanged) rebuildSceneForm(*s);
    if (s->paramsVersion != lastParamsVersion_) {
        lastParamsVersion_ = s->paramsVersion;
        for (ParamForm* f : forms_) f->refresh(*s);
        updateModeVisibility(s->mode);
        for (QAction* a : samplesGroup_->actions()) a->setChecked(a->text().toStdString() == s->sceneName);
    }
    if (sceneChanged) {
        plots_->setScene(s->sceneName); // drops the series, restores the scene's channel selection
        lastSceneName_ = s->sceneName;
    } else if (s->frame < lastFrame_) {
        plots_->clear();
    }
    if (s->frame != lastFrame_ && s->frame > 0) plots_->append(s->time, s->plots, s->probe);
    lastFrame_ = s->frame;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastInfoUpdate_ > 120) {
        lastInfoUpdate_ = now;
        updateInfo(*s);
    }

    const bool wanted = auto_.preset < 0 ? s->sceneName.rfind(SceneBuilder::sceneName(), 0) == 0 // "Конструктор[: file]"
                                         : s->sceneName == samples()[auto_.preset].name;
    if (auto_.active && wanted && int(s->frame) >= auto_.frames && ++auto_.settle == 1) {
        ctrl_->setRunning(false);
        QTimer::singleShot(300, this, [this] {
            if (!auto_.shot.isEmpty()) (auto_.wholeWindow ? windowImage() : view_->grabFramebuffer()).save(auto_.shot);
            if (!auto_.csv.isEmpty()) plots_->writeCsv(auto_.csv);
            qApp->quit();
        });
    }
}

// The window as it is on the screen: a widget grab draws the 3D view without its painted overlay
// (the caption, the navigation cube), so the view's own frame is laid over it.
QImage MainWindow::windowImage() {
    QImage shot = grab().toImage();
    QPainter p(&shot);
    p.drawImage(QRect(view_->mapTo(this, QPoint(0, 0)), view_->size()), view_->grabFramebuffer());
    for (QWidget* c : view_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) // the play banner, ▶ ⏸ ■
        if (c->isVisible()) c->render(&p, c->mapTo(this, QPoint(0, 0)), QRegion(), QWidget::DrawChildren); // rounded: no square backdrop
    return shot;
}

void MainWindow::runAutomation(int preset, int frames, const QString& shot, const QString& csv) {
    preset = std::clamp(preset, -1, int(samples().size()) - 1);
    if (preset < 0) {
        startBuilder();
        if (auto_.throughCamera) builder_->lookThroughActiveCamera();
    } else {
        builder_->showSample();
        ctrl_->post([preset](Simulation& s) { loadSample(s, preset); });
    }
    auto_.active = !shot.isEmpty() || !csv.isEmpty();
    if (!auto_.active) {
        ctrl_->setRunning(preset >= 0); // a ready-made scene from the command line runs at once
        return;
    }
    auto_.preset = preset;
    auto_.frames = std::max(1, frames);
    auto_.shot = shot;
    auto_.csv = csv;
    ctrl_->setRealtimeLimit(false);
    if (preset < 0 && auto_.editOnly) { // the authored scene as it is: no simulation
        QTimer::singleShot(900, this, [this] {
            (auto_.wholeWindow ? windowImage() : view_->grabFramebuffer()).save(auto_.shot);
            qApp->quit();
        });
        return;
    }
    if (preset < 0) builder_->play();
    else ctrl_->setRunning(true);
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------
void MainWindow::importMesh() {
    QString path = QFileDialog::getOpenFileName(this, "Импорт 3D-модели", QString(), "3D-модели (*.obj *.stl);;Все файлы (*)");
    if (path.isEmpty()) return;
    std::string p = path.toStdString();
    QPointer<MainWindow> self(this);
    ctrl_->post([p, self](Simulation& s) {
        std::string err;
        if (s.loadCustomMesh(p, err)) return;
        QString msg = QString::fromStdString(err);
        QMetaObject::invokeMethod(qApp, [self, msg] {
            if (self) QMessageBox::warning(self, "Импорт модели", "Не удалось загрузить модель:\n" + msg);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::importModel() {
    const QString path = QFileDialog::getOpenFileName(this, "Модель как форма", examplesDir() + "/models", "3D-модели (*.obj *.stl)");
    if (path.isEmpty()) return;
    QString error;
    if (!builder_->importModel(path, error)) QMessageBox::warning(this, "Модель", "Не удалось загрузить модель:\n" + error);
}

void MainWindow::exportSurfaceLoads() {
    QString path = QFileDialog::getSaveFileName(this, "Нагрузки по полигонам", "surface_loads.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    std::string p = path.toStdString();
    QPointer<MainWindow> self(this);
    ctrl_->post([p, path, self](Simulation& s) {
        bool empty = s.surfaceLoads().triangles.empty();
        bool ok = !empty && saveSurfaceLoadsCsv(p, s.surfaceLoads());
        QMetaObject::invokeMethod(qApp, [self, ok, empty, path] {
            if (!self) return;
            if (empty) QMessageBox::information(self, "Экспорт", "Нет тела в аэротрубе (или расчёт не запущен).");
            else if (!ok) QMessageBox::warning(self, "Экспорт", "Не удалось записать файл.");
            else self->statusBar()->showMessage("Сохранено: " + path, 4000);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::exportCsv() {
    if (plots_->empty()) {
        QMessageBox::information(this, "Экспорт", "Нет данных: запустите расчёт.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Экспорт графиков", "results.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    if (!plots_->writeCsv(path)) QMessageBox::warning(this, "Экспорт", "Не удалось записать файл.");
    else statusBar()->showMessage("Сохранено: " + path, 4000);
}

void MainWindow::screenshot() {
    QImage img = view_->grabFramebuffer();
    QString def = QString("realflow_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QString path = QFileDialog::getSaveFileName(this, "Сохранить скриншот", def, "PNG (*.png)");
    if (!path.isEmpty() && img.save(path)) statusBar()->showMessage("Сохранено: " + path, 4000);
}

void MainWindow::showHelp() {
    QMessageBox box(this);
    box.setWindowTitle("Управление и методы");
    box.setTextFormat(Qt::RichText);
    box.setText(
        "<h3>Управление</h3>"
        "<b>Пробел</b> — старт/пауза, <b>S</b> — шаг, <b>R</b> — сброс, <b>F</b> — показать всю сцену.<br>"
        "Камера: <b>зажатое колесо</b> — вращение, <b>Shift+колесо</b> или ПКМ — сдвиг, прокрутка — масштаб.<br>"
        "<b>ЛКМ</b> (в сценах с газом) — возмущение: из точки экрана строится луч "
        "(обратная матрица проекция×вид), он пересекается с плоскостью сечения; "
        "скорость газа = скорость движения этой точки × «Сила».<br><br>"
        "<h3>Сетка Навье–Стокса (газ, дым)</h3>"
        "Разнесённая MAC-сетка: u, v, w на гранях, p, дым, T — в центрах ячеек. Шаг: "
        "источники → адвекция (полу-лагранжева RK2 / MacCormack) → силы (плавучесть Буссинеска, "
        "сохранение вихрей) → вязкость → проекция давления (PCG) → div u ≈ 0.<br>"
        "Качество проекции видно в таблице: «Макс. |div u| после проекции».<br><br>"
        "<h3>Частицы SPH и твёрдые тела</h3>"
        "Position Based Fluids, ядра poly6/spiky, соседи через хеш-сетку, столкновения с моделями через BVH; "
        "твёрдые тела — импульсный решатель контактов, широкая фаза — BVH.");
    box.exec();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "О программе",
                       "<b>PhysRealFlow 0.2</b><br>Сетка Навье–Стокса, частицы SPH, BVH, твёрдые тела.<br>"
                       "C++17 · Qt 6 · OpenGL 3.3");
}
