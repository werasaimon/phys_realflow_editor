// Shadows by the checkbox, pressed the way a person presses them (see SelfTest.h):
//   - a spotlight with «Отбрасывает тени» ticked puts a dark patch on the floor behind a cube, and
//     with the tick off the patch is gone (pixels of the picture, seen through a scene camera);
//   - the same for a lamp (its shadow comes from a cube of distances, not a depth map);
//   - the checkbox: shown for every kind of light, off for a new lamp and spotlight and on for a new
//     sun, one undo step, kept by a saved file;
//   - the budget: five lamps with shadows ticked, four cast one, the status bar names the fifth.
// The floor and the cube have no role: plain geometry, drawn as the scenery while the scene plays -
// in play the lights' wireframes are hidden, so no line of theirs crosses the measured pixels.
// With a directory, the pictures shadows-spot-on.png, shadows-spot-off.png, shadows-lamp-on.png and
// shadows-lamp-off.png.
#include "SelfTestSupport.h"

#include <QCheckBox>
#include <QImage>
#include <QStatusBar>

#include <cmath>

using namespace rf;
using namespace selftest;

namespace {

// The stage: a floor, a 0.4 m cube on it, one light in front of the cube and above it, and a scene
// camera to the side that sees the floor behind the cube. `behind` is where the cube's shadow falls
// (the ray from the light through the cube's centre meets the floor), `beside` the floor 0.6 m to the
// side of it, lit either way.
struct ShadowStage {
    uint32_t light = 0, cam = 0;
    Vector3 behind, beside;
};

// The floor's top and the cube's centre (the cube stands on the floor).
const float kFloorTop = 0.02f;
const Vector3 kCubeCentre(0.0f, kFloorTop + 0.2f, 0.0f);

ShadowStage shadowStage(SceneBuilder& b, LightKind kind, bool shadows) {
    ShadowStage st;
    b.newScene();
    const uint32_t floor = made(b, b.createActions()[4]), cube = made(b, b.createActions()[0]);
    b.editEntity(floor, [](Entity& e) { e.size = Vector3(3.0f, 0.02f, 3.0f), e.position = Vector3(0.0f, 0.01f, 0.0f); });
    b.editEntity(cube, [](Entity& e) { e.size = Vector3(0.4f), e.position = kCubeCentre; });
    const bool spot = kind == LightKind::Spot;
    const Vector3 at = spot ? Vector3(0.0f, 0.8f, 1.2f) : Vector3(0.0f, 0.6f, 0.7f);
    st.light = made(b, b.lightActions()[spot ? 2 : 1]);
    b.editLight(st.light, [&](Light& l) {
        l.position = at;
        l.rotationDeg = shining(Vector3(0.0f, kFloorTop, -0.3f) - at);
        l.coneDeg = 70, l.softnessDeg = 5, l.range = spot ? 6.0f : 4.0f, l.intensity = 2;
        l.color = Vector3(1.0f);
        l.shadows = shadows;
    });
    // 1. Where the shadow falls: along the ray from the light through the cube's centre, to the floor.
    const float t = (at.y - kFloorTop) / (at.y - kCubeCentre.y);
    st.behind = at + (kCubeCentre - at) * t;
    st.beside = st.behind + Vector3(0.6f, 0.0f, 0.0f);
    // 2. A camera to the side, 1.2 m up, looking at the floor between the two points.
    st.cam = made(b, b.cameraAction());
    const Vector3 look = st.behind + Vector3(0.3f, 0.0f, 0.0f);
    b.editCamera(st.cam, [&](Camera& k) {
        k.position = Vector3(2.0f, 1.2f, st.behind.z);
        const Vector3 f = normalize(look - k.position);
        k.rotationDeg = Vector3(std::asin(f.y), std::atan2(-f.x, -f.z), 0.0f) * (180.0f / kPi);
        k.fovDeg = 50;
    });
    b.select(0);
    b.lookThrough(st.cam);
    return st;
}

// The floor's brightness behind the cube and beside it, once the light's snapshot shows `shadows`,
// and what one frame costs then (milliseconds): the mean of 10 grabs after 2 unmeasured ones. A grab
// always draws the frame (a repaint of an unchanged view may not), plus a read-back of its pixels -
// the same with a shadow or without, so the difference of two readings is the shadow's price.
struct Reading {
    float behind = 0, beside = 0, frameMs = 0;
};

float frameMs(Viewport* v) {
    for (int k = 0; k < 2; ++k) v->grabFramebuffer(); // warm: a rebuilt shader, a new map
    QElapsedTimer t;
    t.start();
    for (int k = 0; k < 10; ++k) v->grabFramebuffer();
    return float(t.nsecsElapsed()) * 1e-6f / 10.0f;
}

Reading readFloor(Viewport* v, const ShadowStage& st, bool shadows) {
    for (int tries = 0; tries < 60; ++tries) { // frame by frame until the edit has reached the picture
        const uint64_t now = v->snapshot() ? v->snapshot()->frame : 0;
        waitFrames(v, now + 1, 500);
        const auto* s = v->snapshot().get();
        if (s && !s->lights.empty() && s->lights.front().shadows == shadows) break;
    }
    pump(200);
    const QImage img = v->grabFramebuffer();
    return {brightnessAt(v, img, st.behind), brightnessAt(v, img, st.beside), frameMs(v)};
}

// One light kind, shadows on then off, while the scene plays (the wireframes hidden).
void testLightShadow(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, LightKind kind, const QString& dir) {
    const bool spot = kind == LightKind::Spot;
    const ShadowStage st = shadowStage(b, kind, true);
    b.play();
    waitFrames(v, 5, 15000);
    const Reading on = readFloor(v, st, true);
    if (!dir.isEmpty()) windowShot(w).save(dir + (spot ? "/shadows-spot-on.png" : "/shadows-lamp-on.png"));
    b.editLight(st.light, [](Light& l) { l.shadows = false; });
    const Reading off = readFloor(v, st, false);
    if (!dir.isEmpty()) windowShot(w).save(dir + (spot ? "/shadows-spot-off.png" : "/shadows-lamp-off.png"));
    b.stop();
    b.lookThrough(0);
    pump(100);
    std::printf("  %s: floor behind the cube / beside it, shadows on %.0f / %.0f, off %.0f / %.0f; one frame %.0f ms with "
                "the shadow, %.0f ms without\n", spot ? "spotlight" : "lamp", on.behind, on.beside, off.behind, off.beside, on.frameMs,
                off.frameMs);
    c.check(on.beside - on.behind > 25.0f, spot ? "a spotlight with shadows ticked puts a dark patch behind the cube"
                                                : "a lamp with shadows ticked puts a dark patch behind the cube");
    c.check(off.behind - on.behind > 25.0f && std::fabs(off.beside - off.behind) < 25.0f,
            spot ? "the tick off, the spotlight's patch is gone" : "the tick off, the lamp's patch is gone");
}

// The checkbox itself: every kind has it, the defaults, one undo step, a saved file keeps it.
void testShadowCheckbox(Checker& c, QMainWindow& w, SceneBuilder& b) {
    b.newScene();
    pump(150);
    const uint32_t sun = made(b, b.lightActions()[0]), spot = made(b, b.lightActions()[2]), lamp = made(b, b.lightActions()[1]);
    const Light *s = lightOf(b, sun), *p = lightOf(b, spot), *l = lightOf(b, lamp);
    if (!s || !p || !l) {
        c.check(false, "«Свет ▾» makes a sun, a spotlight and a lamp");
        return;
    }
    auto* box = w.findChild<QCheckBox*>("lightShadows");
    const bool shown = box && box->isVisibleTo(&w); // the lamp is selected: its card, its checkbox
    const bool defaults = s->shadows && !p->shadows && !l->shadows;
    if (box) box->click();
    pump(60);
    const bool ticked = lightOf(b, lamp) && lightOf(b, lamp)->shadows;
    SceneGraph back;
    std::string error;
    const bool loaded = back.load(b.graph().save(), error);
    bool kept = false;
    for (const Light& l : back.lights) kept |= l.id == lamp && l.shadows;
    b.undoAction()->trigger();
    pump(60);
    const Light* afterUndo = lightOf(b, lamp); // one undo takes back the tick, not the lamp itself
    const bool undone = afterUndo && !afterUndo->shadows;
    std::printf("  the checkbox: shown for a lamp %d, defaults (sun on, spotlight and lamp off) %d, a click ticks it %d, "
                "saved and loaded %d, one undo takes it back %d (the lamp still there %d)\n", int(shown), int(defaults), int(ticked),
                int(loaded && kept), int(undone), int(afterUndo != nullptr));
    c.check(shown && defaults, "«Отбрасывает тени» is there for a lamp; a new sun casts, a new lamp and spotlight do not");
    c.check(ticked && loaded && kept && undone, "a click ticks it, a saved file keeps it, one undo takes it back");
}

// Five lamps ask for a shadow; four get one, and the status bar says which one has none and why. They
// hang together over a cube, each a little dimmer than the one before: the fifth, the dimmest, loses.
void testShadowBudget(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    const uint32_t cube = made(b, b.createActions()[0]);
    b.editEntity(cube, [](Entity& e) { e.position = Vector3(0.0f, 0.2f, 0.0f); });
    for (int k = 0; k < 5; ++k) {
        const uint32_t lamp = made(b, b.lightActions()[1]);
        b.editLight(lamp, [k](Light& l) {
            l.position = Vector3(0.1f * float(k), 1.2f, 0.0f);
            l.intensity = 1.0f - 0.1f * float(k);
            l.shadows = true;
        });
    }
    b.select(0);
    pump(400);
    const std::vector<int> without = v->lightsWithoutShadow();
    const QString message = w.statusBar()->currentMessage();
    std::printf("  five lamps with shadows ticked: %zu without one (index %d); the status bar: %s\n", without.size(),
                without.empty() ? -1 : without.front(), qPrintable(message));
    c.check(without.size() == 1 && without.front() == 4 && message.contains("без тени") && message.contains("Лампа 5"),
            "five lamps ask for shadows: four get them, the dimmest is named in the status bar");
}

} // namespace

int runShadowTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    testShadowCheckbox(c, w, b);
    testLightShadow(c, w, b, v, LightKind::Spot, shotsDir);
    testLightShadow(c, w, b, v, LightKind::Point, shotsDir);
    testShadowBudget(c, w, b, v);
    return c.failures;
}
