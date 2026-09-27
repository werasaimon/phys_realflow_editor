// The play mode made unmistakable, and two safety nets (see MainWindow.h, docs/ui-research.md):
//   - while the scene plays the view gets an amber frame and a banner "ИГРА — правки не сохранятся",
//     and big ▶ ⏸ ■ buttons wait at the bottom of the view (PlayOverlay);
//   - K keeps what the simulation did (SceneBuilder::keepSimulationPoses, Unreal's "Keep Simulation
//     Changes");
//   - Ctrl+K finds any command by name (CommandSearch);
//   - a white window: if the GPU's first frames come back all one colour, the editor offers to start
//     again with the software renderer, and remembers the choice (Вид → Рендер на процессоре).
#include "MainWindow.h"

#include "CommandSearch.h"
#include "PlayOverlay.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTimer>

#include <algorithm>

void MainWindow::buildPlayExtras() {
    keepAct_ = new QAction("Сохранить положения из симуляции", this);
    keepAct_->setObjectName("keepSimulation");
    keepAct_->setShortcut(Qt::Key_K);
    keepAct_->setToolTip("Положения тел сейчас останутся после ■ Стоп (выбранных, или всех) — K");
    connect(keepAct_, &QAction::triggered, builder_, &SceneBuilder::keepSimulationPoses);
    QMenu* sim = menuBar()->findChild<QMenu*>("simulationMenu");
    if (sim) sim->addAction(keepAct_);
    else addAction(keepAct_);
    playOverlay_ = new PlayOverlay(view_, playAct_, pauseAct_, stopAct_, keepAct_);

    search_ = new CommandSearch(this);
    searchAct_ = new QAction("Найти команду…", this);
    searchAct_->setObjectName("commandSearch");
    searchAct_->setShortcut(QKeySequence("Ctrl+K"));
    connect(searchAct_, &QAction::triggered, search_, &CommandSearch::open);
    QMenu* help = menuBar()->findChild<QMenu*>("helpMenu");
    if (help) help->insertAction(help->actions().value(0), searchAct_);
    else addAction(searchAct_);

    auto* cpu = new QAction("Рендер на процессоре (после перезапуска)", this);
    cpu->setObjectName("softwareRender");
    cpu->setCheckable(true);
    cpu->setChecked(QSettings().value("render/softwareGL", false).toBool());
    connect(cpu, &QAction::toggled, this, [](bool on) { QSettings().setValue("render/softwareGL", on); });
    menuBar()->findChild<QMenu*>("viewMenu")->addAction(cpu);
}

// The frame, the banner and which of K / ▶ ⏸ ■ make sense now; called on every change of mode.
void MainWindow::updatePlayOverlay() {
    if (!playOverlay_) return;
    const SceneBuilder::Mode m = builder_->mode();
    const bool builderPlay = !builder_->showsSample() && m != SceneBuilder::Mode::Edit;
    view_->setPlayFrame(builderPlay);
    playOverlay_->setState(builderPlay && m == SceneBuilder::Mode::Playing, builderPlay && m == SceneBuilder::Mode::Paused, builderPlay);
    keepAct_->setEnabled(builderPlay);
}

// A frame that is all one colour: nothing was drawn (a white window). A grid of samples is enough.
bool MainWindow::looksBlank(const QImage& frame) {
    if (frame.isNull() || frame.width() < 8 || frame.height() < 8) return true;
    const QRgb first = frame.pixel(0, 0);
    int spread = 0;
    for (int j = 0; j < 24; ++j)
        for (int i = 0; i < 24; ++i) {
            const QRgb c = frame.pixel(i * (frame.width() - 1) / 23, j * (frame.height() - 1) / 23);
            spread = std::max({spread, std::abs(qRed(c) - qRed(first)), std::abs(qGreen(c) - qGreen(first)),
                               std::abs(qBlue(c) - qBlue(first))});
        }
    return spread < 4;
}

// On the GPU only (the software renderer is the cure): if the view shows nothing, one question.
// `pretend`: the self-test's way to see the question without a broken driver.
void MainWindow::checkBlankView(bool pretend) {
    if (!pretend && (view_->softwareRenderer() || !looksBlank(view_->grabFramebuffer()))) return;
    auto* box = new QMessageBox(QMessageBox::Warning, "Окно пустое?",
                                "Похоже, видеокарта не нарисовала сцену: окно белое или одноцветное. Так бывает, "
                                "когда драйвер занят другой программой (например, видеоредактором).\n\n"
                                "Перезапустить редактор с рендером на процессоре? Выбор запомнится "
                                "(Вид → Рендер на процессоре).",
                                QMessageBox::NoButton, this);
    box->setObjectName("blankViewDialog");
    box->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* restart = box->addButton("Перезапустить на процессоре", QMessageBox::AcceptRole);
    box->addButton("Нет, всё видно", QMessageBox::RejectRole);
    connect(restart, &QPushButton::clicked, this, [] {
        QSettings().setValue("render/softwareGL", true);
        QStringList args = QCoreApplication::arguments().mid(1);
        args << "--software-gl";
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
        qApp->quit();
    });
    box->open(); // not modal to the rest: the window keeps working
}
