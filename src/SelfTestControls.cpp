// The controls of docs/controls.md, pressed the way a person presses them: the selection (click,
// Shift+click, the box, Ctrl+A), what works on all of it (move, duplicate, hide, delete), the axis
// keys during a drag, the camera (the point under the cursor stays put when orbiting and zooming,
// the right button with W flies, the navigation cube looks from the top), Esc stops the scene, and
// the Blender scheme moves the camera with the middle button. Each check prints one line.
#include "SelfTestSupport.h"

#include "MainWindow.h"

#include <QAction>
#include <QKeyEvent>
#include <QLabel>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace rf;
using namespace selftest;

namespace {

void click(Viewport* v, const QPointF& p, Qt::KeyboardModifiers m = Qt::NoModifier) {
    sendMouse(v, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, m);
    sendMouse(v, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, m);
    pump(30);
}

// A drag with one button from a to b in steps, the keys held all the way.
void drag(Viewport* v, const QPointF& a, const QPointF& b, Qt::MouseButton button, Qt::KeyboardModifiers m = Qt::NoModifier) {
    sendMouse(v, QEvent::MouseButtonPress, a, button, button, m);
    for (int i = 1; i <= 10; ++i) sendMouse(v, QEvent::MouseMove, a + (b - a) * (i / 10.0), Qt::NoButton, button, m);
    sendMouse(v, QEvent::MouseButtonRelease, b, button, Qt::NoButton, m);
    pump(30);
}

void key(Viewport* v, int k, Qt::KeyboardModifiers m = Qt::NoModifier, QEvent::Type type = QEvent::KeyPress) {
    QKeyEvent e(type, k, m);
    QCoreApplication::sendEvent(v, &e);
}

void act(QMainWindow& w, const char* name) {
    if (auto* a = w.findChild<QAction*>(name)) a->trigger();
    pump(30);
}

QPointF onScreen(Viewport* v, const Vector3& p) { return toPoint(v->gizmoView().project(p)); }

const Entity* entity(const SceneBuilder& b, uint32_t id) {
    for (const Entity& e : b.graph().entities)
        if (e.id == id) return &e;
    return nullptr;
}

// Three cubes in a row on the floor, framed; returns their ids.
std::vector<uint32_t> threeCubes(SceneBuilder& b) {
    b.newScene();
    std::vector<uint32_t> ids;
    for (int i = 0; i < 3; ++i) {
        b.createActions()[0]->trigger();
        ids.push_back(last(b).id);
        b.editEntity(ids.back(), [i](Entity& e) { e.position = Vector3(-0.6f + 0.6f * float(i), 0.1f, 0.0f); });
    }
    b.select(0);
    b.frameAll();
    pump(250);
    return ids;
}

void testSelection(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    const std::vector<uint32_t> ids = threeCubes(b);
    trigger(w, "toolSelect"); // Q: no gizmo over the objects' centres
    const QPointF pa = onScreen(v, entity(b, ids[0])->position), pb = onScreen(v, entity(b, ids[1])->position);
    click(v, pa);
    click(v, pb, Qt::ShiftModifier);
    c.check(b.selection().size() == 2 && b.isSelected(ids[0]) && b.isSelected(ids[1]), "Shift+click adds a second object to the selection");
    click(v, pb, Qt::ControlModifier);
    c.check(b.selection().size() == 1 && b.isSelected(ids[0]), "Ctrl+click on a selected object takes it away");
    // The box from empty space above-left of the first cube to just past the second one.
    const QPointF from(std::min(pa.x(), pb.x()) - 70, std::min(pa.y(), pb.y()) - 90), to(pb.x() + 12, std::max(pa.y(), pb.y()) + 30);
    sendMouse(v, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 10; ++i) sendMouse(v, QEvent::MouseMove, from + (to - from) * (i / 10.0), Qt::NoButton, Qt::LeftButton);
    pump(60);
    if (!dir.isEmpty()) windowShot(w).save(dir + "/controls-box-select.png");
    sendMouse(v, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    pump(30);
    std::printf("  box from (%.0f, %.0f) to (%.0f, %.0f): %zu selected\n", from.x(), from.y(), to.x(), to.y(), b.selection().size());
    c.check(b.selection().size() == 2 && b.isSelected(ids[0]) && b.isSelected(ids[1]) && !b.isSelected(ids[2]),
            "a drag from empty space draws a box: the two cubes inside are selected, the third is not");
    act(w, "actionSelectAll");
    c.check(b.selection().size() == 3, "Ctrl+A selects every object (the locked floor stays out)");
    act(w, "actionSelectNone");
    c.check(b.selection().empty(), "Ctrl+Shift+A selects nothing");
    trigger(w, "toolMove");
}

void testActionsOnSelection(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    const std::vector<uint32_t> ids = threeCubes(b);
    b.select(ids[0]);
    b.toggleSelected(ids[1]);
    pump(100);
    const Vector3 a0 = entity(b, ids[0])->position, b0 = entity(b, ids[1])->position;
    trigger(w, "toolMove");
    pump(100);
    const GizmoView gv = v->gizmoView();
    const Vector2 m0 = gv.project(v->gizmo().position() + v->gizmo().axis(0) * (0.6f * v->gizmo().armLength(gv)));
    const Vector2 dir = normalize(gv.project(v->gizmo().position() + Vector3(1, 0, 0)) - gv.project(v->gizmo().position()));
    sendMouse(v, QEvent::MouseMove, toPoint(m0), Qt::NoButton, Qt::NoButton);
    drag(v, toPoint(m0), toPoint(m0 + dir * 60.0f), Qt::LeftButton);
    const float da = entity(b, ids[0])->position.x - a0.x, db = entity(b, ids[1])->position.x - b0.x;
    std::printf("  two selected, the X arrow dragged 60 px: they moved %.3f and %.3f m\n", double(da), double(db));
    c.check(da > 0.03f && std::fabs(da - db) < 1e-5f, "the gizmo moves every selected object by the same step");
    const size_t before = b.graph().entities.size();
    act(w, "actionDuplicate");
    c.check(b.graph().entities.size() == before + 2 && b.selection().size() == 2 && !b.isSelected(ids[0]),
            "Ctrl+D copies both, and the copies become the selection");
    act(w, "actionHide");
    const size_t hidden = size_t(std::count_if(b.graph().entities.begin(), b.graph().entities.end(), [](const Entity& e) { return !e.visible; }));
    act(w, "actionUnhide");
    const bool allBack = std::all_of(b.graph().entities.begin(), b.graph().entities.end(), [](const Entity& e) { return e.visible; });
    c.check(hidden == 2 && allBack, "H hides the selected objects, Alt+H shows everything again");
    b.select(ids[2]);
    act(w, "actionDelete");
    c.check(!entity(b, ids[2]) && b.graph().entities.size() == before + 1, "Delete removes the selected object");
}

// During a free drag (the white centre circle) Y limits the move to the Y axis.
void testAxisKeys(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    const std::vector<uint32_t> ids = threeCubes(b);
    b.select(ids[1]);
    trigger(w, "toolMove");
    pump(150);
    const Vector3 start = entity(b, ids[1])->position;
    const QPointF centre = onScreen(v, v->gizmo().position());
    sendMouse(v, QEvent::MouseMove, centre, Qt::NoButton, Qt::NoButton);
    sendMouse(v, QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 6; ++i) sendMouse(v, QEvent::MouseMove, centre + QPointF(8.0 * i, -10.0 * i), Qt::NoButton, Qt::LeftButton);
    const QString hint = v->mouseHint();
    key(v, Qt::Key_Y);
    sendMouse(v, QEvent::MouseMove, centre + QPointF(50, -64), Qt::NoButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, centre + QPointF(50, -64), Qt::LeftButton, Qt::NoButton);
    pump(50);
    const Vector3 end = entity(b, ids[1])->position;
    std::printf("  free drag, then Y: moved %.3f %.3f %.3f m; the hint said: %s\n", double(end.x - start.x), double(end.y - start.y),
                double(end.z - start.z), qPrintable(hint));
    c.check(end.y - start.y > 0.02f && end.x == start.x && end.z == start.z && hint.contains("X / Y / Z"),
            "Y during a drag keeps the move on the Y axis; the status line tells about X / Y / Z");
}

// The camera: orbiting (right button) and zooming (wheel) keep the point under the cursor in place.
void testCamera(Checker& c, SceneBuilder& b, Viewport* v) {
    const std::vector<uint32_t> ids = threeCubes(b);
    const Vector3 top = entity(b, ids[0])->position + Vector3(0, 0.1f, 0); // the top face of the first cube
    const QPointF p = onScreen(v, top);
    const QVector3D pivot = v->pointUnderCursor(p);
    const Vector3 pv(pivot.x(), pivot.y(), pivot.z());
    drag(v, p, p + QPointF(70, 25), Qt::RightButton);
    const float orbitMiss = length(v->gizmoView().project(pv) - Vector2(float(p.x()), float(p.y())));
    QWheelEvent wheel(p, v->mapToGlobal(p), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    const float before = v->camera().distance();
    QCoreApplication::sendEvent(v, &wheel);
    const float zoomMiss = length(v->gizmoView().project(pv) - Vector2(float(p.x()), float(p.y())));
    std::printf("  orbit: the point under the cursor moved %.2f px; zoom %.2f -> %.2f m: it moved %.2f px\n", double(orbitMiss),
                double(before), double(v->camera().distance()), double(zoomMiss));
    c.check(orbitMiss < 1.0f, "a right-button drag orbits around the point under the cursor: it stays under the cursor");
    c.check(zoomMiss < 1.0f && v->camera().distance() < before, "the wheel zooms towards the cursor: the point under it stays");
}

// The right button held with W flies forward; the navigation cube's +Y ball looks from the top.
void testFlyAndNavCube(Checker& c, Viewport* v) {
    const QPointF mid(v->width() * 0.5, v->height() * 0.6);
    const QVector3D eye0 = v->camera().eye(), forward = v->camera().forward();
    sendMouse(v, QEvent::MouseButtonPress, mid, Qt::RightButton, Qt::RightButton);
    key(v, Qt::Key_W);
    const QString hint = v->mouseHint();
    pump(400);
    key(v, Qt::Key_W, Qt::NoModifier, QEvent::KeyRelease);
    sendMouse(v, QEvent::MouseButtonRelease, mid, Qt::RightButton, Qt::NoButton);
    const float flown = QVector3D::dotProduct(v->camera().eye() - eye0, forward);
    std::printf("  right button + W for 0.4 s: the eye went %.2f m forward\n", double(flown));
    c.check(flown > 0.1f && hint.contains("Полёт"), "right button + W flies forward; the status line lists the flight keys");
    const QPointF ball = v->navCube().ballCentre(2, v->camera().view(), v->size());
    click(v, ball);
    std::printf("  navigation cube, +Y clicked: pitch %.1f degrees\n", double(v->camera().pitch()));
    c.check(v->camera().pitch() > 88.0f, "a click on the cube's Y ball looks at the scene from the top");
}

void testEscAndScheme(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    threeCubes(b);
    b.play();
    pump(200);
    act(w, "actionEscape");
    c.check(b.editing(), "Esc while the scene plays: stop, the scene is as it was before ▶");
    act(w, "scheme1"); // Как в Blender
    auto* scale = w.findChild<QAction*>("toolScale");
    auto* del = w.findChild<QAction*>("actionDelete");
    const float yaw0 = v->camera().yaw();
    const QPointF mid(v->width() * 0.5, v->height() * 0.5);
    drag(v, mid, mid + QPointF(60, 0), Qt::RightButton);
    const float afterRight = v->camera().yaw();
    drag(v, mid, mid + QPointF(60, 0), Qt::MiddleButton);
    const float afterMiddle = v->camera().yaw();
    c.check(v->controlScheme() == ControlScheme::Blender && scale && scale->shortcut().isEmpty() && del &&
                del->shortcuts().contains(QKeySequence(Qt::Key_X)) && afterRight == yaw0 && afterMiddle != yaw0,
            "«Как в Blender»: the middle button orbits, the right one does not, X deletes, R is free for turning");
    act(w, "scheme0");
    auto* local = w.findChild<QAction*>("toolLocal");
    c.check(v->controlScheme() == ControlScheme::Simple && scale && scale->shortcut() == QKeySequence(Qt::Key_R) && local &&
                local->shortcut() == QKeySequence(Qt::Key_L),
            "back to «Простая»: R scales, L switches world / own axes");
    auto* hint = w.findChild<QLabel*>("mouseHint");
    std::printf("  status line: %s\n", hint ? qPrintable(hint->text()) : "(none)");
    c.check(hint && hint->text().contains("ЛКМ"), "the status bar says what the mouse buttons do");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/controls-nav-cube-status.png");
}

} // namespace

int runControlTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    const ControlScheme saved = v->controlScheme();
    testSelection(c, w, b, v, shotsDir);
    testActionsOnSelection(c, w, b, v);
    testAxisKeys(c, w, b, v);
    testCamera(c, b, v);
    testFlyAndNavCube(c, v);
    testEscAndScheme(c, w, b, v, shotsDir);
    if (auto* a = w.findChild<QAction*>(QString("scheme%1").arg(int(saved)))) a->trigger(); // the user's own choice back
    return c.failures;
}
