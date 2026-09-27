// The keys and the mouse schemes of the editor (see MainWindow.h and docs/controls.md): the "Правка"
// menu holds every key as an action, so the key is written next to it in the menu and in its
// tooltip; "Вид → Управление" switches the mouse scheme; the status bar says what the buttons do.
// Many objects at once live here too: groups, arrays, instances, the selection helpers and the
// "Клонировать" popover after a Shift + gizmo drag; and the cameras: "Вид → Камеры" (the editor's
// view or a camera of the scene), Esc out of a camera. The toolbar is in MainWindowToolbar.cpp.
#include "MainWindow.h"

#include "ClonePopover.h"
#include "ControlScheme.h"
#include "RoleBar.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QAction* keyAction(QMenu* menu, const char* id, const QString& text, const QList<QKeySequence>& keys) {
    QAction* a = menu->addAction(text);
    a->setObjectName(id);
    a->setShortcuts(keys);
    a->setShortcutContext(Qt::WindowShortcut);
    return a;
}

} // namespace

// Selecting, deleting, copying, hiding and framing - the same keys as most 3D packages.
void MainWindow::buildEditMenu() {
    auto* menu = new QMenu("&Правка", this);
    menuBar()->insertMenu(menuBar()->findChild<QMenu*>("viewMenu")->menuAction(), menu);
    menu->addAction(builder_->undoAction());
    menu->addAction(builder_->redoAction());
    menu->addSeparator();
    connect(keyAction(menu, "actionSelectAll", "Выбрать всё", {QKeySequence("Ctrl+A")}), &QAction::triggered, builder_, &SceneBuilder::selectAll);
    connect(keyAction(menu, "actionSelectNone", "Снять выделение", {QKeySequence("Ctrl+Shift+A")}), &QAction::triggered, this,
            [this] { builder_->select(0); });
    menu->addSeparator();
    deleteAct_ = keyAction(menu, "actionDelete", "Удалить", {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
    connect(deleteAct_, &QAction::triggered, builder_, &SceneBuilder::removeSelected);
    duplicateAct_ = keyAction(menu, "actionDuplicate", "Дублировать", {QKeySequence("Ctrl+D")});
    connect(duplicateAct_, &QAction::triggered, builder_, &SceneBuilder::duplicateSelected);
    connect(keyAction(menu, "actionHide", "Скрыть выбранное", {QKeySequence(Qt::Key_H)}), &QAction::triggered, builder_, &SceneBuilder::hideSelected);
    connect(keyAction(menu, "actionUnhide", "Показать скрытое", {QKeySequence("Alt+H")}), &QAction::triggered, builder_, &SceneBuilder::unhideAll);
    menu->addSeparator();
    connect(keyAction(menu, "actionGroup", "Сгруппировать", {QKeySequence("Ctrl+G")}), &QAction::triggered, builder_, &SceneBuilder::groupSelected);
    connect(keyAction(menu, "actionUngroup", "Разгруппировать", {QKeySequence("Ctrl+Shift+G")}), &QAction::triggered, builder_,
            &SceneBuilder::ungroupSelected);
    connect(keyAction(menu, "actionMakeArray", "Сделать массивом…", {}), &QAction::triggered, builder_, &SceneBuilder::makeArrayOfSelected);
    addSelectHelpers(menu);
    menu->addSeparator();
    connect(keyAction(menu, "actionFrameSelected", "Показать выбранное (без выбора — всё)", {QKeySequence(Qt::Key_F)}), &QAction::triggered, this,
            [this] { builder_->showsSample() ? view_->frameScene() : builder_->frameSelected(); });
    connect(keyAction(menu, "actionFrameAll", "Показать всю сцену", {QKeySequence(Qt::Key_Home)}), &QAction::triggered, this,
            [this] { builder_->showsSample() ? view_->frameScene() : builder_->frameAll(); });
    menu->addSeparator();
    auto* esc = keyAction(menu, "actionEscape", "Стоп / снять выделение", {QKeySequence(Qt::Key_Escape)});
    esc->setToolTip("Идёт симуляция — стоп и сцена как до ▶; в правке — снять выделение");
    connect(esc, &QAction::triggered, this, [this] {
        if (builder_->lookingThrough()) builder_->lookThrough(0); // first out of the camera, as Blender's Esc
        else if (builder_->showsSample() || !builder_->editing()) builder_->stop();
        else builder_->select(0);
    });
    buildViewKeys(menu);
}

// What works on many objects at once: a group, an array, an instance's link. For the context menu.
void MainWindow::addManyActions(QMenu* menu) {
    const uint32_t id = builder_->selectedId();
    const rf::SceneGraph& g = builder_->graph();
    const bool group = std::any_of(g.groups.begin(), g.groups.end(), [id](const rf::Group& x) { return x.id == id; });
    const bool array = std::any_of(g.arrays.begin(), g.arrays.end(), [id](const rf::ArrayObject& x) { return x.id == id; });
    const bool shape = std::any_of(g.entities.begin(), g.entities.end(), [id](const rf::Entity& x) { return x.id == id; });
    menu->addAction("Сгруппировать", QKeySequence("Ctrl+G"), builder_, &SceneBuilder::groupSelected);
    if (group) menu->addAction("Разгруппировать", QKeySequence("Ctrl+Shift+G"), builder_, &SceneBuilder::ungroupSelected);
    if (shape) menu->addAction(objectIcon(ObjectIcon::ArrayLine, 20), "Сделать массивом…", builder_, &SceneBuilder::makeArrayOfSelected);
    if (array) menu->addAction("Разобрать на объекты", this, [this, id] { builder_->explodeArray(id); });
    if (shape && builder_->masterOf(id) != id) menu->addAction("Отвязать экземпляр", this, [this, id] { builder_->unlinkInstance(id); });
}

// "Выбрать все такие же", "Выбрать все с компонентом ▸", "Инвертировать выбор".
void MainWindow::addSelectHelpers(QMenu* menu) {
    QAction* similar = menu->addAction("Выбрать все такие же", builder_, &SceneBuilder::selectSimilar);
    similar->setObjectName("actionSelectSimilar");
    similar->setToolTip("Та же форма с теми же компонентами, или экземпляры того же образца");
    QMenu* byRole = menu->addMenu("Выбрать все с компонентом");
    byRole->setObjectName("menuSelectByRole");
    for (int k = 0; k < int(RoleIcon::Count); ++k) {
        const RoleIcon role = RoleIcon(k);
        byRole->addAction(roleIcon(role, 20), roleTitle(role), this, [this, role] { builder_->selectWithRole(role); });
    }
    menu->addAction("Инвертировать выбор", builder_, &SceneBuilder::invertSelection)->setObjectName("actionInvertSelection");
}

// After a Shift + gizmo drag: how many copies, and of what kind (ClonePopover.h).
void MainWindow::showClonePopover(const QPointF& pos) {
    if (!builder_->clonePending()) return;
    auto* popover = new ClonePopover(this);
    popover->allowKinds(builder_->cloneAllows(SceneBuilder::CloneKind::Instance), builder_->cloneAllows(SceneBuilder::CloneKind::Array));
    connect(popover, &ClonePopover::accepted, builder_, [this](int copies, int kind) {
        builder_->finishClone(copies, SceneBuilder::CloneKind(kind));
    });
    connect(popover, &ClonePopover::cancelled, builder_, &SceneBuilder::cancelClone);
    popover->popup(view_->mapToGlobal(pos.toPoint()));
}

// The axis views (numpad, Ctrl: the opposite side) and the gizmo's size (+ / -).
void MainWindow::buildViewKeys(QMenu* menu) {
    QMenu* views = menu->addMenu("Вид вдоль оси");
    const struct { const char* text; int key; int ball; } axisViews[] = {
        {"Сверху", Qt::Key_7, 2}, {"Снизу", Qt::Key_7, 3}, {"Спереди", Qt::Key_1, 4}, {"Сзади", Qt::Key_1, 5}, {"Справа", Qt::Key_3, 0}, {"Слева", Qt::Key_3, 1}};
    for (int i = 0; i < 6; ++i) {
        const int ctrl = (i % 2) ? int(Qt::ControlModifier) : 0; // the opposite side: Ctrl, as in Blender
        QAction* a = views->addAction(axisViews[i].text);
        a->setShortcut(QKeySequence(int(Qt::KeypadModifier) | ctrl | axisViews[i].key));
        const int ball = axisViews[i].ball;
        connect(a, &QAction::triggered, this, [this, ball] { view_->viewAlong(ball); });
    }
    QAction* bigger = menu->addAction("Гизмо крупнее");
    bigger->setShortcuts({QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal), QKeySequence(int(Qt::KeypadModifier) | Qt::Key_Plus)});
    connect(bigger, &QAction::triggered, this, [this] { view_->gizmo().setScreenScale(view_->gizmo().screenScale() * 1.15f), view_->update(); });
    QAction* smaller = menu->addAction("Гизмо мельче");
    smaller->setShortcuts({QKeySequence(Qt::Key_Minus), QKeySequence(int(Qt::KeypadModifier) | Qt::Key_Minus)});
    connect(smaller, &QAction::triggered, this, [this] { view_->gizmo().setScreenScale(view_->gizmo().screenScale() / 1.15f), view_->update(); });
}

// Вид → Управление: the three mouse schemes.
void MainWindow::buildControlsMenu() {
    QMenu* menu = menuBar()->findChild<QMenu*>("viewMenu")->addMenu("Управление");
    schemeGroup_ = new QActionGroup(this);
    const ControlScheme schemes[] = {ControlScheme::Simple, ControlScheme::Blender, ControlScheme::MayaUnity};
    const char* tips[] = {"ЛКМ — выбор, ПКМ — осмотр, СКМ — панорама, колесо — зум к курсору; Alt+кнопки как в Maya",
                          "СКМ — вращать, Shift+СКМ — панорама, Ctrl+СКМ — зум; G / R / S, X — удалить, Shift+D — копия",
                          "Alt+ЛКМ — вращать, Alt+СКМ — панорама, Alt+ПКМ — зум; ПКМ + WASD — полёт"};
    for (int i = 0; i < 3; ++i) {
        QAction* a = menu->addAction(controlSchemeName(schemes[i]));
        a->setObjectName(QString("scheme%1").arg(i));
        a->setCheckable(true);
        a->setToolTip(tips[i]);
        schemeGroup_->addAction(a);
        const ControlScheme s = schemes[i];
        connect(a, &QAction::triggered, this, [this, s] { applyControlScheme(s); });
    }
    menu->setToolTipsVisible(true);
    applyControlScheme(savedControlScheme());
}

// The scheme's keys: "Как в Blender" has G / R / S transforms (so R is not the scale tool), X
// deletes and Shift+D duplicates; the others keep Q / W / E / R and Ctrl+D.
void MainWindow::applyControlScheme(ControlScheme s) {
    view_->setControlScheme(s);
    saveControlScheme(s);
    const bool blender = blenderKeys(s);
    if (auto* scale = findChild<QAction*>("toolScale")) scale->setShortcut(blender ? QKeySequence() : QKeySequence(Qt::Key_R));
    deleteAct_->setShortcuts(blender ? QList<QKeySequence>{Qt::Key_X, Qt::Key_Delete, Qt::Key_Backspace}
                                     : QList<QKeySequence>{Qt::Key_Delete, Qt::Key_Backspace});
    duplicateAct_->setShortcuts(blender ? QList<QKeySequence>{QKeySequence("Shift+D"), QKeySequence("Ctrl+D")}
                                        : QList<QKeySequence>{QKeySequence("Ctrl+D")});
    for (QAction* a : schemeGroup_->actions()) a->setChecked(a->text() == controlSchemeName(s));
    appendShortcutTips();
}

// The first start asks once where the user comes from; the window works meanwhile (not modal).
void MainWindow::askControlScheme() {
    if (QSettings().value("controls/asked", false).toBool()) return;
    auto* d = new QDialog(this);
    d->setObjectName("controlsChoice");
    d->setWindowTitle("Управление");
    d->setAttribute(Qt::WA_DeleteOnClose);
    auto* col = new QVBoxLayout(d);
    col->addWidget(new QLabel("Как вы привыкли двигать камеру? Это можно поменять: Вид → Управление."));
    const ControlScheme schemes[] = {ControlScheme::Simple, ControlScheme::Blender, ControlScheme::MayaUnity};
    const char* notes[] = {" (советуем: ЛКМ — выбор, ПКМ — осмотр)", " (СКМ — вращать)", " (Alt + кнопки мыши)"};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QPushButton(controlSchemeName(schemes[i]) + notes[i]);
        b->setObjectName(QString("choice%1").arg(i));
        const ControlScheme s = schemes[i];
        connect(b, &QPushButton::clicked, d, [this, d, s] {
            applyControlScheme(s);
            d->close();
        });
        col->addWidget(b);
    }
    QSettings().setValue("controls/asked", true);
    d->show();
}

// Every action with a key says it in its tooltip: "Дублировать (Ctrl+D)".
void MainWindow::appendShortcutTips() {
    for (QAction* a : findChildren<QAction*>()) {
        if (a->shortcut().isEmpty()) continue;
        QString tip = a->toolTip();
        const int cut = tip.indexOf("  ["); // the key written by an earlier call
        if (cut >= 0) tip.truncate(cut);
        a->setToolTip(tip + "  [" + a->shortcut().toString(QKeySequence::NativeText) + "]");
    }
}

// The viewport's clicks and boxes go to the builder; its hint goes to the status bar.
void MainWindow::connectViewportControls() {
    connect(view_, &Viewport::entityToggled, builder_, &SceneBuilder::toggleSelected);
    connect(view_, &Viewport::boxSelected, this, [this](QRectF rect, Qt::KeyboardModifiers m) {
        builder_->selectInRect(rect, view_->gizmoView(), m & Qt::ShiftModifier, m & Qt::ControlModifier);
    });
    connect(view_, &Viewport::frameSelectedRequested, builder_, &SceneBuilder::frameSelected);
    hintLabel_ = new QLabel;
    hintLabel_->setObjectName("mouseHint");
    statusBar()->addWidget(hintLabel_, 1);
    connect(view_, &Viewport::hintChanged, hintLabel_, &QLabel::setText);
    hintLabel_->setText(view_->mouseHint());
}

// Вид -> Камеры: the editor's own view, then every camera of the scene; the one looked through is ticked.
void MainWindow::buildCamerasMenu() {
    QMenu* menu = menuBar()->findChild<QMenu*>("viewMenu")->addMenu(sceneIcon(SceneIcon::Camera, 20), "Камеры");
    menu->setObjectName("camerasMenu");
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        QAction* editor = menu->addAction("Вид редактора", this, [this] { builder_->lookThrough(0); });
        editor->setCheckable(true);
        editor->setChecked(builder_->lookingThrough() == 0);
        menu->addSeparator();
        for (const rf::Camera& c : builder_->graph().cameras) {
            const uint32_t id = c.id;
            QAction* a = menu->addAction(sceneIcon(SceneIcon::Camera, 20), QString::fromStdString(c.name), this, [this, id] { builder_->lookThrough(id); });
            a->setCheckable(true);
            a->setChecked(builder_->lookingThrough() == id);
        }
        if (builder_->graph().cameras.empty()) menu->addAction("Камер нет — «Камера» на панели сверху")->setEnabled(false);
    });
}
