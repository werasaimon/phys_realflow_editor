// The Laboratory, pressed the way a person presses it (see SelfTest.h):
//   1. F8 opens it: the dock, clicks reported by the view; the timeline strip only while not editing;
//   2. a layer's checkbox changes what the frame draws (primitives appear, and go again);
//   3. a preset switches exactly its layers on, and lights up;
//   4. a click on a contact point opens its card with that point's depth; a click on the cube its card
//      with live numbers from the simulation thread;
//   5. the scrubber shows a kept frame bit for bit and pauses the scene; «Вживую» returns and lets it run;
//   6. while the scene plays the profiler has stages and a step time.
// Each check prints one line; with a directory, the pictures lab-contacts.png, lab-body.png,
// lab-timeline.png.
#include "SelfTestSupport.h"

#include "LabCard.h"
#include "LabLayers.h"
#include "LabPanel.h"
#include "LabTimeline.h"
#include "ProfilerBar.h"

#include <QCheckBox>
#include <QDockWidget>
#include <QPushButton>
#include <QSlider>

#include <cmath>

using namespace rf;
using namespace selftest;

namespace {

struct Lab {
    LabPanel* panel = nullptr;
    LabTimeline* timeline = nullptr;
    QDockWidget* dock = nullptr;
};

Lab labOf(QMainWindow& w, Viewport* v) {
    return {w.findChild<LabPanel*>(), v->findChild<LabTimeline*>(), w.findChild<QDockWidget*>("labDock")};
}

size_t primitives(const Viewport* v) {
    const auto& s = v->snapshot();
    return s ? s->probe.lines.size() + s->probe.points.size() : 0;
}

bool newFrames(Viewport* v, int count) { return waitFrames(v, (v->snapshot() ? v->snapshot()->frame : 0) + uint64_t(count), 15000); }

QPushButton* presetButton(const Lab& lab, const QString& name) {
    for (QPushButton* b : lab.panel->findChildren<QPushButton*>("labPreset"))
        if (b->text() == name) return b;
    return nullptr;
}

// A cube dropped 30 cm onto the floor, turned a little: bodies and contacts to look at.
void droppedCube(QMainWindow& w, SceneBuilder& b) {
    b.newScene();
    pump(100);
    b.createActions()[0]->trigger(); // Куб
    b.editEntity(last(b).id, [](Entity& e) { e.position = Vector3(0.0f, 0.4f, 0.0f); e.rotationDeg = Vector3(0, 30, 0); });
    clickRole(w, RoleIcon::Rigid);
    b.frameSelected(); // the camera close to the cube: its corners on the floor are what the pictures show
    pump(200);
}

void testOpens(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const Lab& lab) {
    droppedCube(w, b);
    trigger(w, "actionLaboratory");
    pump(200);
    const bool open = lab.dock->isVisible() && v->labInspect();
    const bool noStripWhileEditing = !lab.timeline->isVisible();
    b.play();
    newFrames(v, 5);
    c.check(open && noStripWhileEditing && lab.timeline->isVisible(),
            "F8 opens the Laboratory: its dock, clicks reported; the timeline strip appears only once the scene runs");
}

// The drawn primitives once they say `any` (some, or none): the simulation runs a few frames ahead of
// the view, so the first frames after a click may still be ones drawn before it.
size_t primitivesOnceThey(Viewport* v, bool any) {
    for (int k = 0; k < 30 && (primitives(v) > 0) != any; ++k) newFrames(v, 1);
    return primitives(v);
}

void testLayerToggle(Checker& c, Viewport* v, const Lab& lab) {
    presetButton(lab, "Ничего")->click();
    const size_t off = primitivesOnceThey(v, false);
    // «Центр масс»: drawn for every body, asleep or not (a sleeping body has no box and no contacts).
    QCheckBox* box = lab.panel->layerBox(DrawLayer::CentreOfMass);
    box->click();
    const size_t on = primitivesOnceThey(v, true);
    box->click();
    const size_t offAgain = primitivesOnceThey(v, false);
    std::printf("  primitives a frame: all off %zu, «Центр масс» on %zu, off again %zu\n", off, on, offAgain);
    c.check(off == 0 && on > 0 && offAgain == 0 && Probe::layers() == 0, "a layer's checkbox makes the frame draw it, and stop again");
}

void testPreset(Checker& c, const Lab& lab) {
    QPushButton* contacts = presetButton(lab, "Контакты");
    contacts->click();
    uint32_t want = 0;
    for (const LabPreset& p : labPresets())
        if (p.name == "Контакты") want = p.mask;
    const bool boxes = lab.panel->layerBox(DrawLayer::ContactPoints)->isChecked() &&
                       lab.panel->layerBox(DrawLayer::PenetrationDepth)->isChecked() && !lab.panel->layerBox(DrawLayer::BodyAabbs)->isChecked();
    c.check(Probe::layers() == want && boxes && contacts->isChecked(), "the preset «Контакты» switches exactly its four layers on and lights up");
}

// The contact nearest to the eye (lowest on the screen): a corner of the cube on the floor, not hidden.
int visibleContact(const Viewport* v) {
    const auto& s = v->snapshot();
    int best = -1;
    float lowest = -1;
    for (int i = 0; i < int(s->contacts.size()); ++i) {
        const Vector2 p = v->gizmoView().project(s->contacts[size_t(i)].position);
        if (p.x > 0 && p.y > 0 && p.x < v->width() && p.y < v->height() && p.y > lowest) lowest = p.y, best = i;
    }
    return best;
}

// The camera close to the cube where it is now (it has fallen from where it was framed), the way a
// person zooms in on what they look at.
void lookAtCube(Viewport* v) {
    for (const auto& body : v->snapshot()->bodies)
        if (body.movable) v->setFocusBox(AABB(body.pos - Vector3(0.3f), body.pos + Vector3(0.3f)));
    v->frameScene();
    pump(150);
}

void click(Viewport* v, const QPointF& p) {
    sendMouse(v, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    pump(120);
}

void testContactCard(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const Lab& lab, const QString& dir) {
    // The cube dropped again, the «Контакты» layers on: its first frames on the floor, before it falls
    // asleep (a sleeping pair is not solved, so it has no contact points). Pause at the first contacts.
    b.stop();
    pump(150);
    b.play();
    for (int k = 0; k < 400 && (!v->snapshot() || v->snapshot()->contacts.empty()); ++k) newFrames(v, 1);
    b.pause();
    pump(250);
    lookAtCube(v);
    const int i = visibleContact(v);
    if (i < 0) return c.check(false, "the landing cube has a contact point in view");
    const auto s = v->snapshot();
    const RenderSnapshot::ContactInfo contact = s->contacts[size_t(i)];
    click(v, toPoint(v->gizmoView().project(contact.position)));
    // The card rounds to hundredths of a millimetre: it must read the snapshot's depth to within half of one.
    const double want = double(contact.depth) * 1000.0;
    const QString shown = lab.panel->card()->value("Глубина");
    bool isNumber = false;
    const double read = QString(shown).replace(',', '.').toDouble(&isNumber);
    std::printf("  contact %d of %zu: depth %.4f mm in the snapshot, the card shows %s\n", i + 1, s->contacts.size(), want,
                qPrintable(shown));
    c.check(lab.panel->card()->title().startsWith("Точка контакта") && isNumber && std::abs(read - want) <= 0.0051 &&
                v->labMarkerShown(),
            "a click on a contact point opens its card with that point's depth, and rings it in the view");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/lab-contacts.png");
}

void testBodyCard(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const Lab& lab, const QString& dir) {
    const auto s = v->snapshot();
    const RenderSnapshot::Body* cube = nullptr;
    for (const auto& body : s->bodies)
        if (body.movable) cube = &body;
    if (!cube) return c.check(false, "the cube's body is in the frame");
    presetButton(lab, "Всё о телах")->click();
    click(v, toPoint(v->gizmoView().project(cube->pos + Vector3(0.0f, 0.05f, 0.0f))));
    pump(400); // the live numbers come back from the simulation thread
    const QString mass = lab.panel->card()->value("Масса");
    std::printf("  the cube's card: mass %s kg, speed %s m/s, %s\n", qPrintable(mass), qPrintable(lab.panel->card()->value("|v|")),
                qPrintable(lab.panel->card()->value("Сон")));
    c.check(lab.panel->card()->title() == "Тело" && !mass.isEmpty() && !mass.startsWith("—"),
            "a click on the cube opens its card with live numbers from the simulation (mass, speed, sleep)");
    if (!dir.isEmpty()) {
        b.play(); // a few frames with the bodies' layers drawn
        newFrames(v, 3);
        b.pause();
        pump(200);
        windowShot(w).save(dir + "/lab-body.png");
    }
}

void testScrubber(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const Lab& lab, const QString& dir) {
    b.play();
    newFrames(v, 40);
    const int kept = lab.timeline->count();
    auto* slider = lab.timeline->findChild<QSlider*>("labScrubber");
    const int k = kept / 2;
    slider->setValue(k); // what dragging the handle does
    pump(250);
    const auto shown = v->snapshot(), recorded = lab.timeline->frameAt(k);
    bool exact = shown && recorded && shown->bodies.size() == recorded->bodies.size() && shown->frame == recorded->frame;
    for (size_t i = 0; exact && i < shown->bodies.size(); ++i) exact = same(shown->bodies[i].pos, recorded->bodies[i].pos);
    const bool paused = b.mode() == SceneBuilder::Mode::Paused;
    std::printf("  timeline: %d frames kept (%.2f MB); position %d shows frame %llu exactly: %s; the scene paused: %s\n", kept,
                double(lab.timeline->bytes()) / 1048576.0, k + 1, (unsigned long long)(recorded ? recorded->frame : 0),
                exact ? "yes" : "no", paused ? "yes" : "no");
    c.check(kept >= 30 && lab.timeline->showingPast() && exact && paused, "the scrubber shows a kept frame bit for bit and pauses the scene");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/lab-timeline.png");
    lab.timeline->findChild<QPushButton*>("labLive")->click();
    pump(150);
    const bool live = !lab.timeline->showingPast() && v->snapshot() == lab.timeline->newest();
    const bool runs = b.mode() == SceneBuilder::Mode::Playing;
    newFrames(v, 3);
    c.check(live && runs, "«Вживую» shows the newest frame again and the scene runs on");
}

void testProfiler(Checker& c, Viewport* v, const Lab& lab) {
    newFrames(v, 10);
    const ProfilerBar* p = lab.panel->profiler();
    double biggest = 0;
    QString name;
    for (const auto& s : p->stages()) // the engine's own timers (not the grey «Вне замеров» remainder)
        if (s.part != "Вне замеров" && s.ms > biggest) biggest = s.ms, name = s.part + " · " + s.name;
    std::printf("  profiler: step %.2f ms, %zu stages, the biggest %s %.2f ms\n", p->stepMs(), p->stages().size(), qPrintable(name), biggest);
    c.check(p->stepMs() > 0 && biggest > 0, "while the scene plays the profiler shows the engine's timed stages and the step time");
}

} // namespace

int runLabTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    const Lab lab = labOf(w, v);
    if (!lab.panel || !lab.timeline || !lab.dock) {
        c.check(false, "the window has the Laboratory's panel, dock and timeline");
        return c.failures;
    }
    testOpens(c, w, b, v, lab);
    testLayerToggle(c, v, lab);
    testPreset(c, lab);
    testContactCard(c, w, b, v, lab, shotsDir);
    testBodyCard(c, w, b, v, lab, shotsDir);
    testScrubber(c, w, b, v, lab, shotsDir);
    testProfiler(c, v, lab);
    b.stop();
    presetButton(lab, "Ничего")->click();
    Probe::watchPair(-1, -1);
    trigger(w, "actionLaboratory"); // closed again: the next tests see the plain window
    pump(200);
    return c.failures;
}
