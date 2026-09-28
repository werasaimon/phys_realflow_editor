// The plots in the window (see PlotPanel.h), and what ties them to the rest of it:
//   1. zero clicks: the first ▶ opens the plots by itself, compact (unless the reader closed them by
//      hand), and selecting a body gives it its own charts;
//   2. what the simulation measures for them (scene/Channels.h): the scene's channels while the
//      plots or the Laboratory are open - closed, nothing is measured and nothing is paid; and the
//      channels of every body the reader looks at - selected in the scene, or clicked in the
//      Laboratory - under the object's name («Куб 1/height»);
//   3. the Laboratory's timeline: its frame is a line through the plots, and a click on a plot takes
//      the timeline to that moment;
//   4. «Сохранить CSV»: the lines on the charts, to a file.
#include "MainWindow.h"

#include "LabTimeline.h"
#include "PlotPanel.h"
#include "SceneBuilder.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

#include <cmath>

// The charts, and under them a quiet bar: «Ещё величины…» and the layouts on the left, the history
// and the file on the right.
QWidget* MainWindow::buildPlots() {
    plots_ = new PlotPanel;
    auto* host = new QWidget;
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(0, 0, 0, 0);
    col->addWidget(plots_, 1);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 4, 0, 0);
    bar->addWidget(plots_->moreButton());
    bar->addSpacing(8);
    bar->addWidget(plots_->arrangementSwitch());
    bar->addStretch(1);
    auto* clear = new QPushButton("Очистить");
    clear->setToolTip("Стереть записанную историю; графики остаются");
    connect(clear, &QPushButton::clicked, plots_, &PlotPanel::clear);
    auto* csv = new QPushButton("Сохранить CSV");
    csv->setObjectName("plotSaveCsv");
    csv->setToolTip("Линии с графиков в файл: время и по столбцу на линию, с именем и единицей");
    connect(csv, &QPushButton::clicked, this, &MainWindow::saveVisibleCsv);
    bar->addWidget(clear);
    bar->addWidget(csv);
    col->addLayout(bar);
    return host;
}

void MainWindow::connectPlots() {
    connect(plots_, &PlotPanel::timeClicked, this, &MainWindow::labSeekTime);
    connect(timeline_, &LabTimeline::showFrame, this, [this](LabTimeline::Frame s) {
        if (s && timeline_->showingPast()) plots_->setCursorTime(s->time);
        else plots_->clearCursor();
    });
    connect(timeline_, &LabTimeline::wentLive, plots_, &PlotPanel::clearCursor);
    connect(builder_, &SceneBuilder::selectionChanged, this, [this] { watchSelected(); });
    connect(builder_, &SceneBuilder::modeChanged, this, [this] { watchSelected(); });
    connect(plots_, &PlotPanel::saveCsvRequested, this, &MainWindow::saveVisibleCsv);
    connect(plots_, &PlotPanel::roomWanted, this, [this](int height) { // the lanes: taller, up to half the window
        auto* dock = findChild<QDockWidget*>("resultsDock");
        if (dock && dock->isVisible() && dock->height() < height) resizeDocks({dock}, {std::min(height, this->height() / 2)}, Qt::Vertical);
    });
    // Closed by the reader's hand (F9, the menu, the dock's ✕): the next ▶ leaves the plots closed.
    connect(graphsAct_, &QAction::triggered, this, [this](bool on) { plotsClosedByHand_ = !on; });
    auto* dock = findChild<QDockWidget*>("resultsDock");
    connect(dock, &QDockWidget::visibilityChanged, this, [this, dock](bool on) {
        if (on || !dock->isHidden() || settingGraphs_ || !graphsAct_->isChecked()) return;
        plotsClosedByHand_ = true;
        setGraphsVisible(false);
    });
}

// Every simulated frame into the plots, in order - the view draws only the newest frame, the plots
// miss none. A new scene: its own charts (and no body watched from the old one); the frame count
// going back: a new run, a new history.
void MainWindow::recordFrames(const std::vector<SimController::Reading>& readings) {
    if (readings.empty() || builder_->editing()) return; // edit mode shows the authored scene, not a run
    for (const SimController::Reading& r : readings) {
        if (r.scene != plotScene_) {
            plots_->setScene(r.scene);
            if (!plotScene_.empty()) forgetWatched();
            plotScene_ = r.scene;
        } else if (r.frame < plotFrame_) {
            plots_->clear();
        }
        if (r.frame != plotFrame_ && r.frame > 0) plots_->append(r.time, r.plots, r.channels, r.measurements);
        plotFrame_ = r.frame;
    }
    if (sceneRunning()) autoOpenPlots(); // the first ▶ shows the plots by itself
}

// The first ▶ opens the plots by themselves, compact - unless the reader closed them by hand (then
// they stay closed) or nobody watches (the automation's pictures keep their layout).
void MainWindow::autoOpenPlots() {
    if (graphsAct_->isChecked() || plotsClosedByHand_ || auto_.active) return;
    setGraphsVisible(true);
    if (auto* dock = findChild<QDockWidget*>("resultsDock")) resizeDocks({dock}, {210}, Qt::Vertical);
}

// --- What the simulation measures -----------------------------------------------------------------

// The scene's channels (its energy, momentum, the density error of its liquid ...) are measured
// while someone looks: the plots or the Laboratory open, or a CSV to be written.
void MainWindow::updateChannelRequests() {
    const bool on = (graphsAct_ && graphsAct_->isChecked()) || (labAct_ && labAct_->isChecked()) || !auto_.csv.isEmpty();
    ctrl_->post([on](rf::Simulation& sim) { sim.channels.scene = on; });
}

// The name a body's channels go under: the object's name in the scene, else «тело №n». A body
// already watched keeps its name; a name another watched body has gets this body's number.
QString MainWindow::watchLabel(int body) const {
    for (const rf::ObjectRef& w : watched_)
        if (w.index == body) return QString::fromStdString(w.label);
    QString label = QString("тело №%1").arg(body);
    if (!builder_->showsSample())
        if (const rf::SceneObject* o = rf::findObject(builder_->graph(), builder_->entityOfBody(body)); o && !o->name.empty())
            label = QString::fromStdString(o->name);
    for (const rf::ObjectRef& w : watched_)
        if (QString::fromStdString(w.label) == label) return label + QString(" №%1").arg(body);
    return label;
}

// A body's channels - its height, speed, energy, the force of its contacts - are measured from the
// next frame on. A body once watched stays watched until the scene is edited or another one loaded.
void MainWindow::watchBody(int body) {
    if (body < 0 || builder_->editing()) return;
    for (const rf::ObjectRef& w : watched_)
        if (w.index == body) return;
    const rf::ObjectRef ref{rf::ObjectRef::Kind::Body, body, watchLabel(body).toStdString()};
    watched_.push_back(ref);
    ctrl_->post([ref](rf::Simulation& sim) { sim.channels.objects.push_back(ref); });
}

// The object selected in the scene: its charts in the plots («Объект: Куб 1»: height, speed), and,
// while the scene runs, its channels measured. Nothing selected: no object charts. Editing again
// forgets the watched bodies (the next run builds the bodies anew); the charts keep the last run.
void MainWindow::watchSelected() {
    const int body = builder_->bodyOfEntity(builder_->selectedId());
    if (builder_->editing()) forgetWatched();
    else watchBody(body);
    const QString label = body >= 0 ? watchLabel(body) : QString();
    const bool recorded = !label.isEmpty() && plots_->series((label + "/height").toStdString()); // the last run's
    plots_->showObject(!builder_->editing() || recorded ? label : QString());
}

void MainWindow::forgetWatched() {
    if (watched_.empty()) return;
    watched_.clear();
    ctrl_->post([](rf::Simulation& sim) { sim.channels.objects.clear(); });
}

// --- The Laboratory's timeline --------------------------------------------------------------------

// A click on a plot: the timeline shows the kept frame nearest to that moment, and the scene pauses
// as when the strip is taken by hand. A moment the timeline no longer keeps (or the Laboratory
// closed): the status bar says so.
void MainWindow::labSeekTime(double t) {
    if (!timeline_ || !timeline_->isVisible() || timeline_->count() == 0) {
        statusBar()->showMessage("Перейти к моменту графика можно при открытой Лаборатории (F8), пока идёт запись", 4000);
        return;
    }
    int best = 0;
    for (int i = 1; i < timeline_->count(); ++i)
        if (std::fabs(timeline_->frameAt(i)->time - t) < std::fabs(timeline_->frameAt(best)->time - t)) best = i;
    if (std::fabs(timeline_->frameAt(best)->time - t) > 0.05) {
        statusBar()->showMessage("Этот момент уже не записан: Лаборатория хранит последние кадры", 4000);
        return;
    }
    if (!timeline_->showingPast()) labScrubStarted();
    timeline_->seek(best);
}

// --- The file -------------------------------------------------------------------------------------

void MainWindow::saveVisibleCsv() {
    if (plots_->empty()) {
        QMessageBox::information(this, "Сохранить CSV", "Нечего сохранять: графики появятся после ▶.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Сохранить графики", "plots.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    if (!plots_->writeVisibleCsv(path)) QMessageBox::warning(this, "Сохранить CSV", "Не удалось записать файл.");
    else statusBar()->showMessage("Сохранено: " + path, 4000);
}
