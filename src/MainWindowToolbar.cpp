// The big bar on top of the window (see MainWindow.h): what you can create (shapes, a model, the
// lights, a camera), then run / pause / stop / step, then undo / redo, and at the right end the text
// buttons Примеры, Коллайдеры, Лаборатория, Графики, Эксперт.
//
// A narrow window never cuts a word ("Кол...еры"): the bar gives up room step by step instead, as
// the ribbons of Office and 3ds Max do - first the captioned buttons sit closer together, then the
// create buttons drop their captions (the icon and the tooltip stay), then run / step / undo do too,
// then the icons get smaller. fitMainToolbar() picks the first step at which everything fits, on
// every resize of the window.
#include "MainWindow.h"

#include "Icons.h"
#include "SceneBuilder.h"

#include <QAction>
#include <QLayout>
#include <QMenu>
#include <QResizeEvent>
#include <QToolBar>
#include <QToolButton>

namespace {

// How a button of the bar looks: `tight` - no minimum width and less padding, `iconOnly` - the
// caption goes too. The button's own style sheet (not a property rule of the window's sheet): setting
// it tells the button that its style changed, so it measures itself again.
void setLook(QToolButton* b, bool tight, bool iconOnly) {
    const Qt::ToolButtonStyle style = iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon;
    const QString sheet = tight ? "QToolButton { min-width: 0; padding: 4px 4px; }" : QString();
    if (b->styleSheet() == sheet && b->toolButtonStyle() == style) return;
    b->setStyleSheet(sheet);
    b->setToolButtonStyle(style);
}

} // namespace

void MainWindow::buildMainToolbar() {
    auto* tb = new QToolBar("Создать", this);
    createBar_ = tb;
    tb->setObjectName("createBar");
    tb->setMovable(false);
    tb->setIconSize(QSize(48, 48));
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    addToolBar(Qt::TopToolBarArea, tb);
    std::vector<QAction*> create = builder_->createActions();
    auto* modelAct = new QAction(shapeIcon(rf::ShapeKind::Mesh, 48), "Модель", this);
    modelAct->setToolTip("Модель из файла OBJ / STL: только форма, роль — плитками справа");
    connect(modelAct, &QAction::triggered, this, &MainWindow::importModel);
    create.push_back(modelAct);
    for (QAction* a : create) tb->addAction(a);
    addLightButtons(tb);
    for (QAction* a : create) createButtons_.push_back(qobject_cast<QToolButton*>(tb->widgetForAction(a)));
    tb->addSeparator();
    for (QAction* a : {playAct_, pauseAct_, stopAct_, stepAct_, builder_->undoAction(), builder_->redoAction()}) {
        if (a == builder_->undoAction()) tb->addSeparator();
        tb->addAction(a);
        runButtons_.push_back(qobject_cast<QToolButton*>(tb->widgetForAction(a)));
    }
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    addCompactButtons(tb);
}

// Compact text buttons at the right end: the ready-made scenes, the colliders, the graphs, the
// expert panels. They keep their words at every width.
void MainWindow::addCompactButtons(QToolBar* tb) {
    auto compact = [tb](QToolButton* b) {
        b->setObjectName("compactButton");
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        tb->addWidget(b);
    };
    auto* samplesButton = new QToolButton;
    samplesButton->setText("Примеры");
    samplesButton->setToolTip("Все готовые сцены картинками: вода, огонь, плазма, токамак, уроки… Нажмите — откроется");
    connect(samplesButton, &QToolButton::clicked, this, &MainWindow::openGallery);
    compact(samplesButton);
    for (QAction* a : {collidersAct_, labAct_, graphsAct_, expertAct_}) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        compact(b);
    }
}

// "Свет ▾" opens its three lights at once (no second click on an arrow); "Камера" beside it.
void MainWindow::addLightButtons(QToolBar* tb) {
    auto* lights = new QToolButton;
    lights->setObjectName("lightsButton");
    lights->setText("Свет ▾");
    lights->setIcon(sceneIcon(SceneIcon::Bulb, 48));
    lights->setIconSize(tb->iconSize());
    lights->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    lights->setToolTip("Свет: солнце, лампа или прожектор. Физика его не видит — он для глаза и скриншотов");
    lights->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(lights);
    for (QAction* a : builder_->lightActions()) menu->addAction(a);
    lights->setMenu(menu);
    tb->addWidget(lights);
    createButtons_.push_back(lights);
    tb->addAction(builder_->cameraAction());
    createButtons_.push_back(qobject_cast<QToolButton*>(tb->widgetForAction(builder_->cameraAction())));
}

// One step of giving up room (see the head of the file): 0 every caption, roomy; 1 every caption,
// tight; 2 the create buttons icons only; 3 run / step / undo too; 4 icons of 32 px instead of 48.
void MainWindow::setToolbarLevel(int level) {
    const int icon = level >= 4 ? 32 : 48;
    createBar_->setIconSize(QSize(icon, icon));
    for (QToolButton* b : createButtons_) {
        if (!b) continue;
        b->setIconSize(QSize(icon, icon));
        setLook(b, level >= 1, level >= 2);
    }
    for (QToolButton* b : runButtons_)
        if (b) setLook(b, level >= 1, level >= 3);
    toolbarLevel_ = level;
}

// The first step at which the whole bar fits the window's width.
void MainWindow::fitMainToolbar() {
    if (!createBar_) return;
    const int room = width() - 8; // the bar's own frame
    for (int level = 0; level <= 4; ++level) {
        setToolbarLevel(level);
        createBar_->layout()->invalidate();
        if (createBar_->sizeHint().width() <= room) return;
    }
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (e->size().width() != e->oldSize().width()) fitMainToolbar();
}
