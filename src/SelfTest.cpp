// The scene builder's self-test (see SelfTest.h). Each check prints one line, as rf_tests does.
#include "SelfTest.h"
#include "SelfTestSupport.h"

#include "Gizmo.h"
#include "MainWindow.h"
#include "RoleBar.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QThread>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

using namespace rf;
using namespace selftest;

namespace {

bool idsUnique(const SceneGraph& g) {
    std::set<uint32_t> ids;
    for (const Entity& e : g.entities)
        if (e.id == 0 || !ids.insert(e.id).second) return false;
    return true;
}

// A pixel where the gizmo reports this handle: along the arrows, then around the rings.
Vector2 handlePoint(const Gizmo& g, const GizmoView& v, GizmoHandle want) {
    const Vector3 c = g.position();
    const float L = g.armLength(v);
    for (float s = 0.35f; s <= 1.0f; s += 0.05f)
        for (int k = 0; k < 3; ++k) {
            const Vector2 p = v.project(c + g.axis(k) * (L * s));
            if (g.hitTest(v, p) == want) return p;
        }
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 96; ++i) {
            const float t = 2 * kPi * i / 96;
            const Vector2 p = v.project(c + (g.axis((k + 1) % 3) * std::cos(t) + g.axis((k + 2) % 3) * std::sin(t)) * g.ringRadius(v));
            if (g.hitTest(v, p) == want) return p;
        }
    return Vector2(-1.0f, -1.0f);
}

GizmoView testView() {
    GizmoView v;
    v.size = QSize(1200, 800);
    v.camera.frame(AABB({-1, 0, -1}, {1, 1.5f, 1}), 50.0f, 25.0f); // looking down a little: +Y faces the eye
    return v;
}

float screenAngle(const Vector2& p, const Vector2& c) { return std::atan2(-(p.y - c.y), p.x - c.x); }

// ---------------------------------------------------------------------------
// The gizmo's arithmetic
// ---------------------------------------------------------------------------
void testEuler(Checker& c) {
    const Vector3 angles[] = {{0, 0, 0}, {30, 45, 60}, {-20, 170, 5}, {89.9f, 10, 20}, {90, 30, 40}, {-90, -45, 10}, {45, -120, -150}};
    float worst = 0;
    for (const Vector3& d : angles) {
        const Matrix3x3 a = quaternionFromEulerDeg(d).toMatrix3x3();
        const Matrix3x3 b = quaternionFromEulerDeg(eulerDegFromQuaternion(quaternionFromEulerDeg(d))).toMatrix3x3();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) worst = std::max(worst, std::fabs(a.m[i][j] - b.m[i][j]));
    }
    std::printf("  Euler -> quaternion -> Euler -> quaternion: worst matrix difference %.1e (gimbal lock at pitch 90 included)\n", double(worst));
    c.check(worst < 1e-3f, "rotation survives the round trip through Euler angles");
}

void testArrowDrag(Checker& c) {
    GizmoView v = testView();
    Gizmo g;
    g.setMode(GizmoMode::Translate);
    const Vector3 start(0.3f, 0.5f, -0.2f);
    g.setTarget(start, Quaternion());
    const float L = g.armLength(v);
    const Vector2 m0 = v.project(start + Vector3(1, 0, 0) * (0.6f * L));
    c.check(g.hitTest(v, m0) == GizmoHandle::AxisX, "the mouse on the middle of the X arrow picks the X arrow");
    g.begin(GizmoHandle::AxisX, v, m0);
    const Vector2 dir = normalize(v.project(start + Vector3(L, 0, 0)) - v.project(start));
    const Vector2 m1 = m0 + dir * 60.0f;
    const GizmoPose pose = g.drag(v, m1, false);
    const float miss = length(v.project(pose.position + Vector3(0.6f * L, 0, 0)) - m1);
    std::printf("  X arrow dragged 60 px: the object moved %.4f m along X; the grabbed point is %.2f px from the cursor\n",
                double(pose.position.x - start.x), double(miss));
    c.check(miss < 1.5f && pose.position.x > start.x && pose.position.y == start.y && pose.position.z == start.z,
            "an arrow drag moves along its axis only and keeps the grabbed point under the cursor");
    const GizmoPose snapped = g.drag(v, m0 + dir * 47.0f, true);
    const float steps = (snapped.position.x - start.x) / 0.1f;
    c.check(std::fabs(steps - std::round(steps)) < 1e-3f, "Ctrl snaps a move to 0.1 m");
    g.end();
}

void testRingTurn(Checker& c) {
    GizmoView v = testView();
    Gizmo g;
    g.setMode(GizmoMode::Rotate);
    const Vector3 start(0.0f, 0.4f, 0.0f);
    g.setTarget(start, Quaternion());
    const Vector2 m0 = handlePoint(g, v, GizmoHandle::AxisY);
    c.check(m0.x >= 0, "a pixel on the Y ring picks the Y ring");
    g.begin(GizmoHandle::AxisY, v, m0);
    const Vector2 centre = v.project(start);
    const float r = length(m0 - centre), a0 = screenAngle(m0, centre);
    GizmoPose pose;
    for (int i = 1; i <= 10; ++i) { // a quarter turn anticlockwise on screen, in steps
        const float a = a0 + 0.5f * kPi * float(i) / 10;
        pose = g.drag(v, centre + Vector2(std::cos(a), -std::sin(a)) * r, false);
    }
    const Vector3 euler = eulerDegFromQuaternion(pose.rotation);
    std::printf("  Y ring turned a quarter anticlockwise on screen: %.2f degrees; the object's yaw %.2f\n", double(g.dragAngleDeg()),
                double(euler.y));
    c.check(std::fabs(g.dragAngleDeg() - 90.0f) < 0.5f && std::fabs(euler.y - 90.0f) < 0.5f, "a quarter turn of the ring is +90 degrees about Y");
    g.drag(v, centre + Vector2(std::cos(a0 + 0.4f), -std::sin(a0 + 0.4f)) * r, true);
    c.check(std::fabs(g.dragAngleDeg() / 15.0f - std::round(g.dragAngleDeg() / 15.0f)) < 1e-3f, "Ctrl snaps a turn to 15 degrees");
    g.end();
}

void testScaleDrag(Checker& c) {
    GizmoView v = testView();
    Gizmo g;
    g.setMode(GizmoMode::Scale);
    const Vector3 start(0.0f, 0.3f, 0.2f);
    g.setTarget(start, Quaternion());
    const float L = g.armLength(v);
    const Vector2 m0 = v.project(start + Vector3(L, 0, 0));
    c.check(g.hitTest(v, m0) == GizmoHandle::AxisX, "the X cube picks the X scale handle");
    g.begin(GizmoHandle::AxisX, v, m0);
    const GizmoPose pose = g.drag(v, v.project(start + Vector3(2 * L, 0, 0)), false);
    std::printf("  X cube dragged to twice its distance: scale %.3f x %.3f x %.3f\n", double(pose.scale.x), double(pose.scale.y), double(pose.scale.z));
    c.check(std::fabs(pose.scale.x - 2.0f) < 0.02f && pose.scale.y == 1.0f && pose.scale.z == 1.0f, "dragging the X cube twice as far scales X by 2");
    const GizmoPose snapped = g.drag(v, v.project(start + Vector3(1.37f * L, 0, 0)), true);
    c.check(std::fabs(snapped.scale.x * 10 - std::round(snapped.scale.x * 10)) < 1e-3f, "Ctrl snaps a scale to 10 %");
    g.end();
}

// Blender's keys: G, then X, then "0.5": exactly half a metre along X; Shift+R, Z, "90": a quarter turn.
void testKeyboardTransform(Checker& c) {
    GizmoView v = testView();
    Gizmo g;
    g.setMode(GizmoMode::Translate);
    const Vector3 start(0.1f, 0.2f, 0.3f);
    g.setTarget(start, Quaternion());
    const Vector2 mouse(700, 350);
    g.beginModal(GizmoMode::Translate, v, mouse);
    g.constrainModal(0, v);
    g.setTyped(true, 0.5f);
    const GizmoPose moved = g.drag(v, mouse + Vector2(40, 10), false);
    g.end();
    c.check(std::fabs(moved.position.x - 0.6f) < 1e-6f && moved.position.y == start.y && moved.position.z == start.z,
            "keyboard: G, X, 0.5 moves exactly 0.5 m along X");
    g.beginModal(GizmoMode::Rotate, v, mouse);
    g.constrainModal(2, v);
    g.setTyped(true, 90.0f);
    const GizmoPose turned = g.drag(v, mouse, false);
    g.end();
    const Vector3 e = eulerDegFromQuaternion(turned.rotation);
    c.check(std::fabs(e.z - 90.0f) < 0.01f && std::fabs(e.x) < 0.01f && std::fabs(e.y) < 0.01f && g.mode() == GizmoMode::Translate,
            "keyboard: Shift+R, Z, 90 turns a quarter about Z; the tool comes back afterwards");
}

// ---------------------------------------------------------------------------
// The builder in the window
// ---------------------------------------------------------------------------
void testEditPlayStop(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    pump(100);
    const Entity& floor = b.graph().entities[0];
    c.check(b.editing() && b.graph().entities.size() == 1 && floor.locked && floor.collider.enabled && !floor.rigid.enabled,
            "a new scene is a locked floor with a collider only (a fixed obstacle), in edit mode");
    b.createActions()[0]->trigger(); // Куб
    const Entity cube = last(b);
    c.check(b.graph().entities.size() == 2 && cube.shape == ShapeKind::Box && entityIsGeometryOnly(cube) &&
                std::fabs(cube.position.y - (0.8f + 0.5f * cube.size.y)) < 1e-6f,
            "the cube button makes geometry only: no component, hanging 0.8 m above the floor (ready to fall)");
    pump(700);
    c.check(b.editing() && same(last(b).position, cube.position) && v->snapshot()->bodies.size() == 2 &&
                same(v->snapshot()->bodies[1].pos, cube.position), "edit mode: the cube does not move (nothing is simulated)");
    b.editEntity(cube.id, [](Entity& e) { e.position.y = 1.0f; });
    clickRole(w, RoleIcon::Rigid);
    pump(700);
    c.check(last(b).rigid.enabled && last(b).position.y == 1.0f && v->snapshot()->bodies[1].pos.y == 1.0f,
            "a rigid cube 1 m up still does not move in edit mode");
    b.play();
    const bool ran = waitFrames(v, 60, 15000);
    float y = 1e9f;
    for (const auto& body : v->snapshot()->bodies)
        if (body.movable) y = body.pos.y;
    std::printf("  after 60 frames of play the cube is at y = %.3f m\n", double(y));
    c.check(ran && y < 0.3f, "play: the cube falls onto the floor");
    b.stop();
    pump(300);
    c.check(b.editing() && last(b).position.y == 1.0f && v->snapshot()->bodies[1].pos.y == 1.0f, "stop: the cube is back where it was authored");
}

void testClothPlayStop(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.createActions()[4]->trigger(); // Плоскость
    clickRole(w, RoleIcon::Cloth);
    c.check(last(b).cloth.pinnedEdges == 16 && last(b).position.y == 1.2f, "cloth on a plane hangs itself up at 1.2 m");
    b.play();
    const bool ran = waitFrames(v, 60, 60000); // a 3 x 3 m cloth is slow on a busy CPU
    float lowest = 1e9f;
    for (const auto& cloth : v->snapshot()->cloths)
        for (const Vector3& p : cloth.positions) lowest = std::min(lowest, p.y);
    std::printf("  the cloth's lowest point after 60 frames: y = %.3f m\n", double(lowest));
    c.check(ran && lowest < 1.0f, "play: the cloth hangs down from its rod");
    b.stop();
    pump(300);
    c.check(b.editing() && v->snapshot()->cloths.empty() && v->snapshot()->bodies.size() == 3, "stop: the cloth is a flat plane again");
    const uint32_t highest = last(b).id;
    b.undoAction()->trigger();
    c.check(!last(b).cloth.enabled, "undo takes the cloth role off");
    b.redoAction()->trigger();
    c.check(last(b).cloth.enabled, "redo puts it back");
    b.createActions()[1]->trigger(); // Сфера
    c.check(last(b).id > highest && idsUnique(b.graph()), "ids are never reused within a session");
}

// A real mouse drag in the window: press on the X arrow of the selected cube, move 80 px, release.
void testMouseDrag(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    b.createActions()[0]->trigger();
    trigger(w, "toolMove");
    pump(300);
    const Entity before = last(b);
    const GizmoView gv = v->gizmoView();
    const Vector2 m0 = handlePoint(v->gizmo(), gv, GizmoHandle::AxisX);
    const Vector2 dir = normalize(gv.project(before.position + Vector3(1, 0, 0)) - gv.project(before.position));
    sendMouse(v, QEvent::MouseMove, toPoint(m0), Qt::NoButton, Qt::NoButton);
    sendMouse(v, QEvent::MouseButtonPress, toPoint(m0), Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 8; ++i) sendMouse(v, QEvent::MouseMove, toPoint(m0 + dir * (10.0f * i)), Qt::NoButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, toPoint(m0 + dir * 80.0f), Qt::LeftButton, Qt::NoButton);
    pump(100);
    const Vector3 after = last(b).position;
    std::printf("  mouse drag of the X arrow by 80 px: x %.3f -> %.3f m\n", double(before.position.x), double(after.x));
    c.check(m0.x >= 0 && after.x - before.position.x > 0.03f && after.y == before.position.y && after.z == before.position.z,
            "the mouse drags the cube along X by its arrow");
    b.undoAction()->trigger();
    c.check(b.graph().entities.size() == 2 && same(last(b).position, before.position), "one Ctrl+Z undoes the whole drag");
}

// Three cubes in a row along one ray: clicks on the same pixel take them nearest first, then round.
void testCyclePicking(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    for (int i = 0; i < 3; ++i) b.createActions()[0]->trigger();
    pump(200);
    const GizmoView gv = v->gizmoView();
    const Vector2 pixel(float(v->width()) * 0.5f, float(v->height()) * 0.55f);
    const Ray ray = gv.ray(pixel);
    const float toFloor = -ray.origin.y / ray.dir.y;
    const size_t n = b.graph().entities.size();
    uint32_t ids[3];
    for (int i = 0; i < 3; ++i) {
        ids[i] = b.graph().entities[n - 3 + size_t(i)].id;
        const Vector3 p = ray.at(toFloor * (0.5f + 0.15f * float(i)));
        b.editEntity(ids[i], [p](Entity& e) { e.position = p; });
    }
    trigger(w, "toolSelect");
    pump(100);
    uint32_t picked[4];
    for (uint32_t& id : picked) {
        sendMouse(v, QEvent::MouseButtonPress, toPoint(pixel), Qt::LeftButton, Qt::LeftButton);
        sendMouse(v, QEvent::MouseButtonRelease, toPoint(pixel), Qt::LeftButton, Qt::NoButton);
        pump(40);
        id = b.selectedId();
    }
    std::printf("  four clicks on one pixel picked cubes %u %u %u %u (in a row: %u %u %u)\n", picked[0], picked[1], picked[2], picked[3],
                ids[0], ids[1], ids[2]);
    c.check(picked[0] == ids[0] && picked[1] == ids[1] && picked[2] == ids[2] && picked[3] == ids[0],
            "clicking the same spot again selects the next object behind, then round to the nearest");
    trigger(w, "toolMove");
}

// A role changed while the scene plays: a falling cube of water becomes jelly in mid-air (its liquid
// particles go, a soft body comes), the floor's body keeps its index; Stop brings the water back.
void testLiveEdit(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    const uint32_t floor = b.graph().entities[0].id;
    b.createActions()[0]->trigger();
    const uint32_t cube = last(b).id;
    b.editEntity(cube, [](Entity& e) {
        e.size = Vector3(0.3f);
        e.position.y = 2.0f;
        e.liquid.enabled = true;
    });
    // Paused right after the start, while the water is surely in the air: the simulation thread of a
    // small scene runs faster than real time, so waiting for drawn frames (or even 0.05 s of
    // simulated time) let the water land before the pause arrived.
    b.play();
    b.pause();
    pump(200);
    const size_t water = v->snapshot()->particles.size() + v->snapshot()->liquid.size();
    float waterLowest = 1e9f;
    for (const Vector3& p : v->snapshot()->liquid) waterLowest = std::min(waterLowest, p.y);
    for (const Vector3& p : v->snapshot()->particles) waterLowest = std::min(waterLowest, p.y);
    const int floorBody = b.bodyOfEntity(floor);
    clickRole(w, RoleIcon::Soft); // the inspector stays live during play (and pause)
    QElapsedTimer waited; // the change reaches the simulation between two of its frames
    waited.start();
    while (v->snapshot()->softMeshes.empty() && waited.elapsed() < 10000) pump(30);
    const auto& s = *v->snapshot();
    const size_t waterAfter = s.particles.size() + s.liquid.size();
    float lowest = 1e9f;
    for (const auto& m : s.softMeshes)
        for (const Vector3& p : m.positions) lowest = std::min(lowest, p.y);
    std::printf("  paused in the air: water particles %zu -> %zu (its lowest point y = %.2f m), soft bodies %zu (lowest point y = %.2f m), floor body %d -> %d\n",
                water, waterAfter, double(waterLowest), s.softMeshes.size(), double(lowest), floorBody, b.bodyOfEntity(floor));
    // The jelly takes the water's place: its lowest point where the water's was (within 0.1 m), in the air.
    c.check(water > 0 && waterAfter < water && !s.softMeshes.empty() && waterLowest > 0.5f && std::fabs(lowest - waterLowest) < 0.1f &&
                b.bodyOfEntity(floor) == floorBody,
            "during play, a falling water cube turns into jelly in mid-air at the water's place; nothing else is rebuilt");
    b.stop();
    pump(300);
    const Entity& back = last(b);
    c.check(b.editing() && back.liquid.enabled && !back.soft.enabled && back.position.y == 2.0f,
            "stop: the scene is as it was before play (the water cube is back), as in Unity");
}

// The right-click menu: "Вращение" and "Локальная" switch the gizmo, and the toolbar follows.
void testContextMenu(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    auto* mw = dynamic_cast<MainWindow*>(&w);
    if (!mw) return c.check(false, "the window is the editor's MainWindow");
    b.newScene();
    b.createActions()[0]->trigger();
    trigger(w, "toolMove");
    pump(100);
    auto find = [](QMenu* m, const QString& text) -> QAction* {
        for (QAction* a : m->actions())
            if (a->text() == text) return a;
        return nullptr;
    };
    QMenu* menu = mw->buildEditContextMenu();
    QAction* rotate = find(menu, "Вращение");
    QAction* local = find(menu, "Локальная (Local)");
    const bool complete = rotate && local && find(menu, "Мировая (World)") && find(menu, "Удалить") && find(menu, "Дублировать");
    if (rotate) rotate->trigger();
    if (local) local->trigger();
    auto* toolbarRotate = w.findChild<QAction*>("toolRotate");
    c.check(complete && v->gizmo().mode() == GizmoMode::Rotate && v->gizmo().local() && toolbarRotate && toolbarRotate->isChecked(),
            "the right-click menu switches the gizmo to rotation and local axes; the toolbar follows");
    delete menu;
    QMenu* again = mw->buildEditContextMenu();
    if (QAction* world = find(again, "Мировая (World)")) world->trigger();
    delete again;
    trigger(w, "toolMove");
    c.check(!v->gizmo().local() && v->gizmo().mode() == GizmoMode::Translate, "and back: world axes, the move tool");
}

QString examplesPath() {
    const QString nextToExe = QCoreApplication::applicationDirPath() + "/examples";
    if (QDir(nextToExe).exists()) return nextToExe;
#ifdef RF_EDITOR_EXAMPLES
    return QStringLiteral(RF_EDITOR_EXAMPLES);
#else
    return QDir::currentPath();
#endif
}

// The authored scene with a few shapes, a model and a cloth, in edit mode and then playing.
void sceneShots(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    b.newScene();
    for (int k = 0; k < 4; ++k) b.createActions()[size_t(k)]->trigger();
    QString error;
    b.importModel(examplesPath() + "/models/house.obj", error);
    b.createActions()[4]->trigger();
    b.editEntity(last(b).id, [](Entity& e) { e.size = Vector3(1.2f, 0.02f, 1.2f); e.position = Vector3(1.4f, 0.03f, -0.4f); });
    clickRole(w, RoleIcon::Cloth);
    for (const Entity& e : b.graph().entities)
        if (e.shape == ShapeKind::Box || e.shape == ShapeKind::Sphere) b.editEntity(e.id, [](Entity& x) { x.rigid.enabled = true; x.collider.enabled = true; });
    b.select(b.graph().entities[1].id);
    pump(500);
    windowShot(w).save(dir + "/edit.png");
    b.play();
    waitFrames(v, 90, 20000);
    windowShot(w).save(dir + "/play.png");
    b.stop();
    pump(200);
}

} // namespace

int runGizmoShots(QMainWindow& w, const QString& dir) {
    auto* b = w.findChild<SceneBuilder*>();
    auto* v = w.findChild<Viewport*>();
    if (!b || !v) return 3;
    QDir().mkpath(dir);
    pump(300);
    b->newScene();
    b->createActions()[0]->trigger();
    b->editEntity(last(*b).id, [](Entity& e) { e.size = Vector3(0.5f); e.position.y = 0.25f; e.rotationDeg = Vector3(0, 25, 0); });
    const struct { const char* tool; const char* name; GizmoHandle hover; } shots[] = {
        {"toolMove", "gizmo-w", GizmoHandle::AxisX}, {"toolRotate", "gizmo-e", GizmoHandle::AxisX}, {"toolScale", "gizmo-r", GizmoHandle::AxisX}};
    int missing = 0;
    for (const auto& s : shots) {
        trigger(w, s.tool);
        pump(250);
        const GizmoView gv = v->gizmoView();
        const Vector2 p = handlePoint(v->gizmo(), gv, s.hover);
        sendMouse(v, QEvent::MouseMove, toPoint(p), Qt::NoButton, Qt::NoButton);
        pump(250);
        const QImage frame = v->grabFramebuffer();
        missing += windowShot(w).save(dir + "/" + s.name + ".png") ? 0 : 1;
        const Vector2 c = gv.project(v->gizmo().position());
        frame.copy(int(c.x) - 230, int(c.y) - 200, 460, 400).save(dir + "/" + s.name + "-close.png");
    }
    // A turn in progress: the pie slice and the angle next to the cursor.
    trigger(w, "toolRotate");
    pump(150);
    const GizmoView gv = v->gizmoView();
    const Vector2 m0 = handlePoint(v->gizmo(), gv, GizmoHandle::AxisY), centre = gv.project(v->gizmo().position());
    const float r = length(m0 - centre), a0 = screenAngle(m0, centre);
    sendMouse(v, QEvent::MouseButtonPress, toPoint(m0), Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 12; ++i) {
        const float a = a0 + 1.05f * float(i) / 12;
        sendMouse(v, QEvent::MouseMove, toPoint(centre + Vector2(std::cos(a), -std::sin(a)) * r), Qt::NoButton, Qt::LeftButton);
    }
    pump(250);
    const QImage frame = v->grabFramebuffer();
    missing += windowShot(w).save(dir + "/gizmo-e-drag.png") ? 0 : 1;
    frame.copy(int(centre.x) - 230, int(centre.y) - 200, 460, 400).save(dir + "/gizmo-e-drag-close.png");
    sendMouse(v, QEvent::MouseButtonRelease, toPoint(centre), Qt::LeftButton, Qt::NoButton);
    trigger(w, "toolMove");
    return missing;
}

int runBuilderSelfTest(QMainWindow& w, const QString& shotsDir) {
    Checker c;
    auto* b = w.findChild<SceneBuilder*>();
    auto* v = w.findChild<Viewport*>();
    if (!b || !v) {
        c.check(false, "the window has a scene builder and a viewport");
        return c.failures;
    }
    pump(400); // the window gets its size, the viewport its first frame
    // RF_EDITOR_TEST=name runs only the groups whose name contains it (as RF_TEST does for rf_tests).
    const QString only = qEnvironmentVariable("RF_EDITOR_TEST");
    auto run = [&](const char* name, const std::function<void()>& group) {
        if (only.isEmpty() || QString(name).contains(only)) group();
    };
    run("gizmo", [&] { testEuler(c), testArrowDrag(c), testRingTurn(c), testScaleDrag(c), testKeyboardTransform(c); });
    run("play", [&] { testEditPlayStop(c, w, *b, v), testClothPlayStop(c, w, *b, v); });
    run("mouse", [&] { testMouseDrag(c, w, *b, v), testCyclePicking(c, w, *b, v); });
    run("menu", [&] { testContextMenu(c, w, *b, v); });
    run("live", [&] { testLiveEdit(c, w, *b, v); });
    run("components", [&] { c.failures += runComponentTests(w, *b, v, shotsDir); });
    run("controls", [&] { c.failures += runControlTests(w, *b, v, shotsDir); });
    if (!shotsDir.isEmpty()) {
        QDir().mkpath(shotsDir);
        c.check(runGizmoShots(w, shotsDir) == 0, "screenshots of the gizmo");
        sceneShots(w, *b, v, shotsDir);
    }
    std::printf(c.failures ? "%d FAILURE(S)\n" : "ALL PASSED\n", c.failures);
    return c.failures;
}
