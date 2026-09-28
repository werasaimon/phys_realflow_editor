// Lights and cameras, pressed the way a person presses them (see SelfTest.h): "Свет ▾" makes a sun,
// a lamp and a spotlight and "Камера" a camera, each where it should appear; a click on the lamp's
// wireframe selects it and the gizmo moves it (a scale leaves it alone); a spotlight aimed at the left
// half of a cube's face lights that half, and turned by the gizmo lights the right half (pixels of
// the software renderer, seen through a scene camera); through a camera the view matrix is the
// camera's frame, moving the view moves the camera, Esc gives the editor's view back; play and stop
// keep the lights, and a light edited while playing reaches the next frame. Each check prints one
// line; with a directory, the pictures lights-edit.png, lights-lit.png and camera-through.png. The
// "Смотрю через:" list in the view's corner names the camera and switches to it (testCameraPicker).
#include "SelfTestSupport.h"

#include <QComboBox>
#include <QImage>

#include <algorithm>
#include <cmath>

using namespace rf;
using namespace selftest;

namespace {

const Camera* cameraOf(const SceneBuilder& b, uint32_t id) {
    for (const Camera& c : b.graph().cameras)
        if (c.id == id) return &c;
    return nullptr;
}

void testCreateAndPick(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    b.frameAll();
    pump(150);
    const uint32_t sun = made(b, b.lightActions()[0]), lamp = made(b, b.lightActions()[1]), spot = made(b, b.lightActions()[2]);
    const uint32_t cam = made(b, b.cameraAction());
    const Light *s = lightOf(b, sun), *l = lightOf(b, lamp), *p = lightOf(b, spot);
    const Camera* k = cameraOf(b, cam);
    c.check(s && l && p && k && s->kind == LightKind::Sun && l->kind == LightKind::Point && p->kind == LightKind::Spot &&
                b.graph().lights.size() == 3 && b.graph().cameras.size() == 1,
            "«Свет ▾» makes a sun, a lamp and a spotlight, «Камера» a camera");
    if (!s || !l || !p || !k) return;
    const Vector3 sd = lightDirection(b.graph(), *s);
    Vector3 eye, forward, up;
    cameraFrame(b.graph(), *k, eye, forward, up);
    const QVector3D viewEye = v->camera().eye();
    std::printf("  sun at y %.2f shines (%.2f %.2f %.2f), shadows %d; lamp at (%.2f %.2f %.2f); camera %.3f m from the view's eye\n", s->position.y,
                sd.x, sd.y, sd.z, int(s->shadows), l->position.x, l->position.y, l->position.z,
                length(eye - Vector3(viewEye.x(), viewEye.y(), viewEye.z())));
    c.check(s->position.y > 1.5f && sd.y < -0.5f && std::fabs(sd.x) + std::fabs(sd.z) > 0.2f && s->shadows,
            "the sun stands high and shines down at a slant, with shadows");
    c.check(std::fabs(l->position.y - 1.5f) < 1e-4f && std::fabs(l->position.x) < 1e-4f, "the lamp hangs 1.5 m over the centre (nothing selected)");
    c.check(length(eye - Vector3(viewEye.x(), viewEye.y(), viewEye.z())) < 1e-3f && dot(forward, normalize(Vector3(0, 0.5f, 0) - eye)) > 0.999f,
            "the camera stands at the editor's eye and looks at the scene's centre");
    c.check(v->sceneMarkers().size() == 4, "edit mode draws all four as wireframes");

    // A click on the lamp's wireframe selects it; the gizmo moves it; a scale does nothing to it.
    b.select(0);
    pump(60);
    const QPointF at = toPoint(v->gizmoView().project(l->position));
    trigger(w, "toolSelect");
    sendMouse(v, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    pump(60);
    c.check(b.selectedId() == lamp, "a click on the lamp's wireframe selects the lamp");
    b.select(lamp);
    const Vector3 before = lightOf(b, lamp)->position;
    b.onGizmoStarted();
    GizmoPose pose;
    pose.mode = GizmoMode::Translate;
    pose.position = before + Vector3(0.4f, 0, 0);
    b.onGizmoMoved(pose);
    b.onGizmoFinished();
    const Vector3 moved = lightOf(b, lamp)->position;
    b.onGizmoStarted();
    pose.mode = GizmoMode::Scale;
    pose.position = moved;
    pose.scale = Vector3(3.0f);
    b.onGizmoMoved(pose);
    b.onGizmoFinished();
    c.check(std::fabs(moved.x - before.x - 0.4f) < 1e-5f && same(lightOf(b, lamp)->position, moved) && lightOf(b, lamp)->range == l->range,
            "the gizmo moves the lamp 0.4 m; a scale leaves it as it was");
    trigger(w, "toolMove");
}

// A cube's face seen through a camera straight ahead; a narrow spotlight aimed at one half of it.
struct SpotStage {
    uint32_t cube = 0, spot = 0, cam = 0;
    Vector3 left{-0.15f, 0.3f, 0.3f}, right{0.15f, 0.3f, 0.3f}, spotAt{0.0f, 0.3f, 1.0f};
};

SpotStage spotStage(SceneBuilder& b) {
    SpotStage st;
    b.newScene();
    st.cube = made(b, b.createActions()[0]);
    b.editEntity(st.cube, [](Entity& e) {
        e.size = Vector3(0.6f);
        e.position = Vector3(0, 0.3f, 0);
        e.color = Vector3(0.8f);
    });
    st.spot = made(b, b.lightActions()[2]);
    b.editLight(st.spot, [&](Light& l) {
        l.position = st.spotAt;
        l.rotationDeg = shining(st.left - st.spotAt);
        l.coneDeg = 20, l.softnessDeg = 3, l.range = 5, l.intensity = 2;
        l.color = Vector3(1.0f);
    });
    st.cam = made(b, b.cameraAction());
    b.editCamera(st.cam, [](Camera& c) {
        c.position = Vector3(0, 0.3f, 2.2f);
        c.rotationDeg = Vector3(0.0f);
        c.fovDeg = 40;
    });
    b.select(0);
    b.lookThrough(st.cam);
    pump(300);
    return st;
}

void testSpotTurnedByGizmo(Checker& c, SceneBuilder& b, Viewport* v) {
    const SpotStage st = spotStage(b);
    QImage img = v->grabFramebuffer();
    const float l0 = brightnessAt(v, img, st.left), r0 = brightnessAt(v, img, st.right);
    b.select(st.spot); // the gizmo turns it about Y until it aims at the right half
    const Light* spot = lightOf(b, st.spot);
    Vector3 p;
    Quaternion q;
    worldPose(b.graph(), *spot, p, q);
    b.onGizmoStarted();
    GizmoPose pose;
    pose.mode = GizmoMode::Rotate;
    pose.position = p;
    pose.rotation = (Quaternion::fromTwoVectors(st.left - st.spotAt, st.right - st.spotAt) * q).normalized();
    b.onGizmoMoved(pose);
    b.onGizmoFinished();
    b.select(0);
    pump(300);
    img = v->grabFramebuffer();
    const float l1 = brightnessAt(v, img, st.left), r1 = brightnessAt(v, img, st.right);
    const Vector3 d = lightDirection(b.graph(), *lightOf(b, st.spot)), want = normalize(st.right - st.spotAt);
    std::printf("  cube face brightness left / right: aimed left %.0f / %.0f, turned right %.0f / %.0f; direction off by %.1e\n", l0, r0, l1, r1,
                length(d - want));
    c.check(l0 > r0 + 25.0f, "a spotlight aimed at the left half of the face lights the left half");
    c.check(r1 > l1 + 25.0f && length(d - want) < 1e-4f, "turned by the gizmo it lights the right half");
    b.lookThrough(0);
}

void testCameraPicker(Checker& c, SceneBuilder& b, Viewport* v, uint32_t cam);

void testLookThrough(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    made(b, b.createActions()[0]);
    const uint32_t cam = made(b, b.cameraAction());
    b.editCamera(cam, [](Camera& k) {
        k.position = Vector3(1.0f, 1.2f, 2.5f);
        k.rotationDeg = Vector3(-20, 35, 10);
        k.fovDeg = 42;
    });
    b.select(0);
    pump(100);
    const QMatrix4x4 editorView = v->camera().view();
    b.lookThrough(cam);
    pump(150);
    Vector3 eye, f, up;
    cameraFrame(b.graph(), *cameraOf(b, cam), eye, f, up);
    QMatrix4x4 want;
    want.lookAt(QVector3D(eye.x, eye.y, eye.z), QVector3D(eye.x + f.x, eye.y + f.y, eye.z + f.z), QVector3D(up.x, up.y, up.z));
    const QMatrix4x4 got = v->camera().view();
    float worst = 0;
    for (int i = 0; i < 16; ++i) worst = std::max(worst, std::fabs(got.constData()[i] - want.constData()[i]));
    std::printf("  through the camera (turned -20 35 10): view matrix off by %.1e, field of view %.1f deg\n", worst, v->camera().fovDeg());
    c.check(worst < 1e-4f && v->lookingThrough() && std::fabs(v->camera().fovDeg() - 42.0f) < 1e-4f && cameraOf(b, cam)->active,
            "through the camera the view matrix is the camera's frame and the lens is its lens");
    v->camera().pan(40.0f, 0.0f); // the mouse moves the view: the camera follows
    v->update();
    pump(200);
    Vector3 eye2, f2, up2;
    cameraFrame(b.graph(), *cameraOf(b, cam), eye2, f2, up2);
    const QVector3D e = v->camera().eye();
    const float follow = length(eye2 - Vector3(e.x(), e.y(), e.z()));
    std::printf("  a pan through the camera moved it %.3f m; it is %.1e m from the view's eye\n", length(eye2 - eye), follow);
    c.check(length(eye2 - eye) > 0.01f && follow < 1e-3f, "moving the view through the camera moves the camera");
    trigger(w, "actionEscape");
    pump(100);
    const QMatrix4x4 back = v->camera().view();
    float diff = 0;
    for (int i = 0; i < 16; ++i) diff = std::max(diff, std::fabs(back.constData()[i] - editorView.constData()[i]));
    c.check(!v->lookingThrough() && b.lookingThrough() == 0 && diff < 1e-5f, "Esc gives the editor's own view back");
    testCameraPicker(c, b, v, cam);
}

// "Смотрю через:" in the corner of the view: it lists the camera, a pick there looks through it,
// and when the view goes back another way (here: the builder, as the menu or Esc do) it follows.
void testCameraPicker(Checker& c, SceneBuilder& b, Viewport* v, uint32_t cam) {
    auto* list = v->findChild<QComboBox*>("cameraPickerList");
    const int row = list ? list->findData(cam) : -1;
    const bool listed = row > 0 && list->currentData().toUInt() == 0;
    if (row > 0) {
        list->setCurrentIndex(row);
        emit list->activated(row); // what a click on that line does
    }
    pump(100);
    const bool picked = b.lookingThrough() == cam && v->lookingThrough();
    b.lookThrough(0);
    pump(100);
    const bool followed = list && list->currentData().toUInt() == 0 && list->currentText() == "Вид редактора";
    std::printf("  the corner list: camera listed %s, picked through the list %s, follows the way back %s\n",
                listed ? "yes" : "no", picked ? "yes" : "no", followed ? "yes" : "no");
    c.check(listed && picked && followed, "the corner list names the camera, switches to it, and follows the way back");
}

void testPlayKeepsLights(Checker& c, SceneBuilder& b, Viewport* v) {
    b.newScene();
    made(b, b.createActions()[0]);
    made(b, b.lightActions()[0]);
    const uint32_t lamp = made(b, b.lightActions()[1]);
    made(b, b.lightActions()[2]);
    b.select(0);
    b.play();
    const bool ran = waitFrames(v, 5, 15000);
    const size_t playing = v->snapshot() ? v->snapshot()->lights.size() : 0;
    const size_t markers = v->sceneMarkers().size();
    b.editLight(lamp, [](Light& l) { l.intensity = 2.5f; });
    // Frame by frame until the lamp shows its new brightness (a loaded PC may take a few frames).
    bool edited = false;
    for (int tries = 0; tries < 40 && !edited; ++tries) {
        const uint64_t now = v->snapshot() ? v->snapshot()->frame : 0;
        waitFrames(v, now + 1, 500);
        if (v->snapshot())
            for (const auto& l : v->snapshot()->lights) edited |= l.kind == int(LightKind::Point) && l.intensity == 2.5f;
    }
    b.stop();
    pump(150);
    c.check(ran && playing == 3 && markers == 0, "playing: the three lights shade the scene, their wireframes hidden (none selected)");
    c.check(edited, "a lamp made brighter while playing is brighter in the next frames");
    c.check(b.editing() && b.graph().lights.size() == 3 && v->snapshot()->lights.size() == 3 && v->sunShadowDrawn(),
            "stop: the lights are all still there, the sun casts its shadow");
}

// The pictures: a lit scene with its lights drawn, the same scene playing, and the view through the camera.
void lightShots(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    b.newScene();
    const uint32_t cube = made(b, b.createActions()[0]), ball = made(b, b.createActions()[1]), cone = made(b, b.createActions()[3]);
    b.editEntity(cube, [](Entity& e) { e.position = Vector3(-0.5f, 0.1f, 0.0f), e.rigid.enabled = e.collider.enabled = true; });
    b.editEntity(ball, [](Entity& e) { e.position = Vector3(0.1f, 0.1f, 0.3f), e.rigid.enabled = e.collider.enabled = true; });
    b.editEntity(cone, [](Entity& e) { e.position = Vector3(0.6f, 0.1f, -0.3f), e.rigid.enabled = e.collider.enabled = true; });
    made(b, b.lightActions()[0]);
    const uint32_t lamp = made(b, b.lightActions()[1]);
    b.editLight(lamp, [](Light& l) { l.position = Vector3(0.8f, 0.9f, 0.6f), l.color = Vector3(1.0f, 0.55f, 0.3f), l.range = 2.5f; });
    const uint32_t spot = made(b, b.lightActions()[2]);
    b.editLight(spot, [](Light& l) { l.position = Vector3(-1.2f, 1.4f, 1.0f), l.rotationDeg = shining(Vector3(0.9f, -1.3f, -1.0f)), l.color = Vector3(0.5f, 0.7f, 1.0f); });
    b.frameAll();
    pump(200);
    const uint32_t cam = made(b, b.cameraAction());
    b.editCamera(cam, [](Camera& k) { // low, from the other side, looking at the shapes
        k.position = Vector3(-1.5f, 0.45f, -1.3f);
        const Vector3 f = normalize(Vector3(0.0f, 0.15f, 0.0f) - k.position);
        k.rotationDeg = Vector3(std::asin(f.y), std::atan2(-f.x, -f.z), 0.0f) * (180.0f / kPi);
        k.fovDeg = 45;
    });
    b.select(spot);
    pump(300);
    windowShot(w).save(dir + "/lights-edit.png");
    b.select(0);
    b.play();
    waitFrames(v, 40, 20000);
    windowShot(w).save(dir + "/lights-lit.png");
    b.stop();
    b.lookThrough(cam);
    pump(300);
    windowShot(w).save(dir + "/camera-through.png");
    b.lookThrough(0);
    pump(100);
}

} // namespace

int runLightTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    testCreateAndPick(c, w, b, v);
    testSpotTurnedByGizmo(c, b, v);
    testLookThrough(c, w, b, v);
    testPlayKeepsLights(c, b, v);
    if (!shotsDir.isEmpty()) lightShots(w, b, v, shotsDir);
    return c.failures;
}
