// The Laboratory in the window (see MainWindow.h): the «Лаборатория» button and F8, the dock with the
// layers, the card and the profiler (LabPanel), the timeline strip on the view (LabTimeline), and what
// a click in the view means while the Laboratory is open:
//   1. a contact point within 10 px of the cursor (the contacts of the frame on screen) - its card:
//      the two bodies, the depth, the normal, the impulses, and «Следить за парой» for GJK / EPA;
//   2. else the body under the cursor - its card: mass, principal moments of inertia, velocity,
//      angular velocity, sleep; live from the simulation thread, or from the kept frame when the
//      timeline shows the past (then only what the frame holds: the pose and the sleep).
// The thing on the card gets a ring in the view, so the numbers and the place belong together.
#include "MainWindow.h"

#include "LabCard.h"
#include "LabPanel.h"
#include "LabTimeline.h"
#include "PlayOverlay.h"
#include "PlotPanel.h"
#include "ProfilerBar.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

#include <cmath>

using rf::Vector3;

namespace {

// A number the Russian way: a decimal comma, `digits` after it; a tiny negative one is plain 0
// («-0,00» reads like a mistake).
QString num(double v, int digits) {
    QString s = QString::number(v, 'f', digits);
    if (s.startsWith('-') && s.toDouble() == 0.0) s.remove(0, 1);
    return s.replace('.', ',');
}
QString vec(const Vector3& v, int digits) {
    return QString("(%1; %2; %3)").arg(num(v.x, digits), num(v.y, digits), num(v.z, digits));
}

// A contact point in words, by its depth and its push. Within 0,05 mm of the surface the bodies touch
// (the solver holds a resting contact there); a negative depth is a gap - a speculative point the
// solver already brakes when it has an impulse, one that only waits when it has none.
QString touchWords(const rf::RenderSnapshot::ContactInfo& c) {
    const float mm = c.depth * 1000.0f;
    if (mm > 0.05f) return "проникли друг в друга";
    if (mm >= -0.05f) return c.normalImpulse > 0 ? "касаются и давят" : "касаются";
    return c.normalImpulse > 0 ? "на подлёте: решатель тормозит" : "зазор: ещё не касаются";
}

// What a body is called in the scene: the object's name, else «тело №n».
QString bodyName(const SceneBuilder& b, int body) {
    if (body < 0) return "неподвижный мир";
    if (b.showsSample()) return QString("тело №%1").arg(body); // a ready-made scene: not the builder's objects
    if (const rf::SceneObject* o = rf::findObject(b.graph(), b.entityOfBody(body)); o && !o->name.empty())
        return QString::fromStdString(o->name) + QString(" (тело №%1)").arg(body);
    return QString("тело №%1").arg(body);
}

} // namespace

// The button and the key, the dock (at the left, hidden), the timeline on the view.
void MainWindow::buildLaboratory() {
    labAct_ = new QAction("Лаборатория", this);
    labAct_->setObjectName("actionLaboratory");
    labAct_->setCheckable(true);
    labAct_->setShortcut(QKeySequence("F8"));
    labAct_->setToolTip("Лаборатория (F8): что считает движок — контакты, силы, деревья, поля; карточка по щелчку, "
                        "запись последних кадров, время каждой стадии");
    labDock_ = new QDockWidget("Лаборатория", this);
    labDock_->setObjectName("labDock");
    lab_ = new LabPanel;
    labDock_->setWidget(lab_);
    labDock_->setMinimumWidth(290);
    addDockWidget(Qt::LeftDockWidgetArea, labDock_);
    if (auto* params = findChild<QDockWidget*>("paramsDock")) tabifyDockWidget(params, labDock_);
    labDock_->hide();
    timeline_ = new LabTimeline(view_);
    menuBar()->findChild<QMenu*>("viewMenu")->addAction(labAct_);
    connectLaboratory();
}

void MainWindow::connectLaboratory() {
    connect(labAct_, &QAction::toggled, this, &MainWindow::setLaboratoryVisible);
    connect(labDock_, &QDockWidget::visibilityChanged, this, [this](bool on) {
        if (!on && labDock_->isHidden()) labAct_->setChecked(false); // closed by its own ✕
    });
    connect(view_, &Viewport::labClicked, this, &MainWindow::onLabClicked);
    connect(lab_->card(), &LabCard::plotRequested, this, &MainWindow::labPlot);
    connect(lab_->profiler(), &ProfilerBar::plotRequested, this, &MainWindow::labPlot);
    connect(timeline_, &LabTimeline::showFrame, this, [this](LabTimeline::Frame s) { view_->setSnapshot(std::move(s)); });
    connect(timeline_, &LabTimeline::scrubStarted, this, &MainWindow::labScrubStarted);
    connect(timeline_, &LabTimeline::wentLive, this, &MainWindow::labWentLive);
    connect(timeline_, &LabTimeline::placed, this, [this](int height) {
        if (playOverlay_) playOverlay_->setBottomInset(height);
    });
}

void MainWindow::setLaboratoryVisible(bool on) {
    labAct_->setChecked(on);
    labDock_->setVisible(on);
    if (on) labDock_->raise();
    view_->setLabInspect(on);
    if (!on) view_->setLabMarker(false);
    lab_->syncFromProbe();
    updateLabTimeline();
}

// The strip is there while the Laboratory is open and something simulated is on screen (not while the
// scene is edited: then there is nothing to replay). Leaving it shows the live frame again, quietly.
void MainWindow::updateLabTimeline() {
    if (!timeline_) return;
    const bool show = labAct_->isChecked() && !builder_->editing();
    if (!show && timeline_->showingPast()) {
        timeline_->blockSignals(true);
        timeline_->goLive();
        timeline_->blockSignals(false);
        labResume_ = false;
    }
    timeline_->setVisible(show);
    if (!show && playOverlay_) playOverlay_->setBottomInset(0);
}

// Every simulated frame: kept by the timeline, timed by the profiler. A new frame while the past is on
// screen means the scene runs again (▶ was pressed): the view follows it live.
// The recording keeps every frame the simulation made, drawn or not (a paused scene publishes the same
// frame again after each command: the timeline keeps it once). The profiler learns each new frame too.
void MainWindow::labRecord(const std::vector<std::shared_ptr<const rf::RenderSnapshot>>& frames) {
    if (!timeline_ || builder_->editing()) return; // edit mode shows the authored scene, not a run
    for (const auto& s : frames) {
        const LabTimeline::Frame before = timeline_->newest();
        timeline_->record(s);
        if (timeline_->newest() == before) continue;
        labFresh_ = true;
        if (labAct_->isChecked()) lab_->profiler()->addFrame(s->probe);
    }
}

// The frame the view is about to draw: the layer boxes follow the Probe; new frames of a running scene
// take a timeline that shows the past back to «Вживую» (someone pressed ▶ Пуск).
bool MainWindow::labOnSnapshot() {
    if (!timeline_) return true;
    if (labAct_->isChecked()) lab_->syncFromProbe();
    if (timeline_->showingPast() && labFresh_ && sceneRunning()) timeline_->goLive();
    labFresh_ = false;
    return !timeline_->showingPast();
}

bool MainWindow::sceneRunning() const {
    return builder_->showsSample() ? ctrl_->isRunning() : builder_->mode() == SceneBuilder::Mode::Playing;
}

void MainWindow::labScrubStarted() {
    if (!sceneRunning()) return;
    labResume_ = true;
    if (builder_->showsSample()) ctrl_->setRunning(false);
    else builder_->pause();
}

void MainWindow::labWentLive() {
    if (!labResume_) return;
    labResume_ = false;
    if (builder_->showsSample()) ctrl_->setRunning(true);
    else builder_->play();
}

void MainWindow::labPlot(const QString& channel) {
    plots_->showChannel(channel.toStdString());
    setGraphsVisible(true);
    statusBar()->showMessage("График «" + channel + "» — внизу, в «Графиках» (F9)", 4000);
}

// 1. The contact nearest to the click on the screen, if within 10 px. 2. Else the body under it.
// 3. Else the card goes back to its hint.
void MainWindow::onLabClicked(QPointF pos) {
    const auto s = view_->snapshot();
    if (!s) return;
    const GizmoView gv = view_->gizmoView();
    int best = -1;
    float bestDist = 10.0f;
    for (int i = 0; i < int(s->contacts.size()); ++i) {
        const rf::Vector2 p = gv.project(s->contacts[size_t(i)].position);
        const float d = std::hypot(p.x - float(pos.x()), p.y - float(pos.y()));
        if (d < bestDist) bestDist = d, best = i;
    }
    if (best >= 0) return showContactCard(*s, best);
    int body;
    Vector3 hit;
    if (view_->pickAnyBody(view_->camera().screenRay(pos, view_->size()), body, hit)) return showBodyCard(body);
    lab_->card()->showHint();
    view_->setLabMarker(false);
}

void MainWindow::showContactCard(const rf::RenderSnapshot& s, int index) {
    // The engine's normal points from B towards A: the solver pushes A along it and B the other way
    // (rigid/RigidWorld.cpp), and the «Нормали» layer draws it so. The card names A and B to match.
    const rf::RenderSnapshot::ContactInfo& c = s.contacts[size_t(index)];
    const QString pair = "A: " + bodyName(*builder_, c.bodyA) + "   ·   B: " + bodyName(*builder_, c.bodyB);
    lab_->card()->showThing(QString("Точка контакта %1 из %2").arg(index + 1).arg(s.contacts.size()), pair,
                            {{"Глубина", num(c.depth * 1000.0f, 2), "мм", ""},
                             {"Касание", touchWords(c), "", ""},
                             {"Нормаль, от B к A", vec(c.normal, 3), "", ""},
                             {"Нормальный импульс", num(c.normalImpulse, 4), "Н·с", ""},
                             {"Импульс трения пятна", num(c.frictionImpulse, 4), "Н·с", ""},
                             {"Где", vec(c.position, 3), "м", ""},
                             {"Кадр", QString::number(s.frame) + ", t = " + num(s.time, 2), "с", ""},
                             {"Контактов в сцене", QString::number(s.probe.value("rigid/contacts", double(s.contacts.size()))), "",
                              "rigid/contacts"}});
    const int a = c.bodyA, b = c.bodyB;
    lab_->card()->setButton("Следить за парой (GJK · EPA)",
                            "Слои «Симплексы GJK», «Многогранник EPA» и «Точки-свидетели» покажут, как движок нашёл этот контакт",
                            [this, a, b] {
                                rf::Probe::watchPair(a, b);
                                lab_->addLayers(rf::Probe::bits(rf::DrawLayer::GjkSimplex, rf::DrawLayer::WitnessPoints));
                                statusBar()->showMessage("Слежу за парой: GJK и EPA рисуются со следующего шага", 4000);
                            });
    view_->setLabMarker(true, c.position);
}

// The body's card: at once from the frame on screen, then (live) the simulation thread's numbers.
void MainWindow::showBodyCard(int body) {
    const auto s = view_->snapshot();
    LabBodyFacts f;
    f.body = body;
    if (s && body < int(s->bodies.size())) {
        f.pos = s->bodies[size_t(body)].pos;
        f.sleeping = s->bodies[size_t(body)].sleeping;
        f.fixed = !s->bodies[size_t(body)].movable;
    }
    fillBodyCard(f);
    view_->setLabMarker(true, f.pos);
    if (timeline_->showingPast() || builder_->editing()) return; // a kept frame: nothing live to ask
    ctrl_->post([this, body](rf::Simulation& sim) {
        LabBodyFacts live;
        live.body = body;
        live.live = true;
        if (body >= 0 && body < int(sim.rigid.bodies().size())) {
            const rf::RigidBody& b = sim.rigid.bodies()[size_t(body)];
            live.alive = b.alive, live.fixed = b.invMass == 0, live.sleeping = b.sleeping, live.sleepIsland = b.sleepIsland;
            live.mass = b.mass, live.pos = b.pos, live.vel = b.vel, live.angVel = b.angVel;
            for (int k = 0; k < 3; ++k) live.inertia[k] = b.invInertiaLocal[k] > 0 ? 1.0f / b.invInertiaLocal[k] : 0.0f;
        }
        QMetaObject::invokeMethod(this, [this, live] { fillBodyCard(live); }, Qt::QueuedConnection);
    });
}

void MainWindow::fillBodyCard(const LabBodyFacts& f) {
    const QString dash = "— только вживую";
    const QString sleep = f.sleeping ? (f.sleepIsland >= 0 ? QString("спит, остров сна %1").arg(f.sleepIsland) : QString("спит"))
                                     : QString("не спит");
    std::vector<LabCard::Row> rows;
    if (f.live && !f.alive) rows.push_back({"Состояние", "удалено", "", ""});
    rows.push_back({"Масса", f.live ? (f.fixed ? QString("∞ (неподвижное)") : num(f.mass, 3)) : dash, f.live && !f.fixed ? "кг" : "", ""});
    rows.push_back({"Моменты инерции", f.live && !f.fixed ? vec(f.inertia, 4) : (f.live ? QString("∞") : dash), f.live && !f.fixed ? "кг·м²" : "", ""});
    rows.push_back({"Скорость", f.live ? vec(f.vel, 3) : dash, f.live ? "м/с" : "", ""});
    rows.push_back({"|v|", f.live ? num(rf::length(f.vel), 3) : dash, f.live ? "м/с" : "", ""});
    rows.push_back({"Угловая скорость", f.live ? vec(f.angVel, 3) : dash, f.live ? "рад/с" : "", ""});
    rows.push_back({"Где", vec(f.pos, 3), "м", ""});
    rows.push_back({"Сон", sleep, "", ""});
    const auto s = view_->snapshot();
    if (s) {
        rows.push_back({"Энергия движения всех тел", num(s->probe.value("rigid/kinetic energy J", 0.0), 3), "Дж", "rigid/kinetic energy J"});
        rows.push_back({"Тел не спит", QString::number(int(s->probe.value("rigid/bodies awake", 0.0))), "", "rigid/bodies awake"});
    }
    lab_->card()->showThing("Тело", bodyName(*builder_, f.body) + (f.live ? "" : " · из сохранённого кадра"), rows);
    lab_->card()->setButton(QString(), QString(), nullptr);
}
