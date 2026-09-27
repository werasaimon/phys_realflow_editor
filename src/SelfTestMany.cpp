// Many objects at once, pressed the way a person presses it (see SelfTest.h and docs/controls.md):
// Shift + a drag of the X arrow by 0.5 m and "4" in the popover make four cubes 0.5, 1.0, 1.5 and
// 2.0 m further; instances follow their master's density; a clone as "Массив" is one array that
// plays as five bodies; three selected cubes cloned twice make six; Ctrl+G and a drag of the group
// move its members; one density typed for three selected cubes goes to all three; and one Ctrl+Z
// takes each of these back. Each check prints one line.
#include "SelfTestSupport.h"

#include "ClonePopover.h"
#include "MainWindow.h"
#include "RuPlural.h"

#include <QAction>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <cmath>

using namespace rf;
using namespace selftest;

namespace {

const Entity* entity(const SceneBuilder& b, uint32_t id) {
    for (const Entity& e : b.graph().entities)
        if (e.id == id) return &e;
    return nullptr;
}

void act(QMainWindow& w, const char* name) {
    trigger(w, name);
    pump(40);
}

// n cubes in a row along X on the floor, framed, nothing selected; returns their ids.
std::vector<uint32_t> cubes(SceneBuilder& b, int n, bool rigid = false) {
    b.newScene();
    std::vector<uint32_t> ids;
    for (int i = 0; i < n; ++i) {
        b.createActions()[0]->trigger();
        ids.push_back(last(b).id);
        b.editEntity(ids.back(), [i, rigid](Entity& e) {
            e.position = Vector3(-0.5f + 0.5f * float(i), 0.1f, 0.0f);
            e.rigid.enabled = e.collider.enabled = rigid;
        });
    }
    b.select(0);
    b.frameAll();
    pump(250);
    return ids;
}

// A drag of the gizmo's X arrow from 60 % of its length by `metres` along X, with these keys held
// from the press to the release (Shift: a clone; Ctrl: steps of 0.1 m, so the distance is exact).
void dragArrowX(Viewport* v, float metres, Qt::KeyboardModifiers keys) {
    const GizmoView gv = v->gizmoView();
    const Gizmo& g = v->gizmo();
    const Vector3 grab = g.position() + g.axis(0) * (0.6f * g.armLength(gv));
    const QPointF from = toPoint(gv.project(grab)), to = toPoint(gv.project(grab + Vector3(metres, 0, 0)));
    sendMouse(v, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
    sendMouse(v, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, keys);
    for (int i = 1; i <= 12; ++i) sendMouse(v, QEvent::MouseMove, from + (to - from) * (i / 12.0), Qt::NoButton, Qt::LeftButton, keys);
    sendMouse(v, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, keys);
    pump(60);
}

ClonePopover* popover(QMainWindow& w) {
    for (ClonePopover* p : w.findChildren<ClonePopover*>("clonePopover"))
        if (p->isVisible()) return p;
    return nullptr;
}

// The popover's answer: how many copies, of which kind (0 Копия, 1 Экземпляр, 2 Массив), OK.
bool answer(QMainWindow& w, int copies, int kind) {
    ClonePopover* p = popover(w);
    if (!p) return false;
    p->findChild<QSpinBox*>("cloneCount")->setValue(copies);
    const char* kinds[] = {"cloneCopy", "cloneInstance", "cloneArray"};
    p->findChild<QRadioButton*>(kinds[kind])->click();
    p->findChild<QPushButton*>("cloneOk")->click();
    pump(80);
    return true;
}

// The window with a popup drawn where it stands (a popup is a window of its own).
QImage windowWithPopup(QMainWindow& w, QWidget* popup) {
    QImage shot = windowShot(w);
    QPainter p(&shot);
    p.drawPixmap(w.mapFromGlobal(popup->pos()), popup->grab());
    return shot;
}

void testShiftClone(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    const std::vector<uint32_t> ids = cubes(b, 1);
    b.select(ids[0]);
    trigger(w, "toolMove");
    pump(150);
    const Vector3 x0 = entity(b, ids[0])->position;
    const size_t before = b.graph().entities.size();
    dragArrowX(v, 0.5f, Qt::ShiftModifier | Qt::ControlModifier);
    c.check(popover(w) != nullptr && b.clonePending(), "Shift at the start of an arrow drag: on release the «Клонировать» popover opens");
    if (popover(w) && !dir.isEmpty()) windowWithPopup(w, popover(w)).save(dir + "/clone-popover.png");
    answer(w, 4, 0);
    bool row = b.graph().entities.size() == before + 4;
    for (int k = 1; k <= 4 && row; ++k) {
        const Entity& e = b.graph().entities[before + size_t(k - 1)];
        std::printf("  copy %d at x %+.5f (original %+.5f)\n", k, double(e.position.x), double(x0.x));
        row = std::fabs(e.position.x - (x0.x + 0.5f * float(k))) < 1e-4f && std::fabs(e.position.y - x0.y) < 1e-6f;
    }
    c.check(row && std::fabs(entity(b, ids[0])->position.x - x0.x) < 1e-6f,
            "4 copies at +0.5, +1.0, +1.5, +2.0 m (±0.1 mm), the original where it was");
    if (!dir.isEmpty()) {
        b.frameAll();
        pump(250);
        windowShot(w).save(dir + "/clone-row.png");
    }
    b.undoAction()->trigger();
    pump(60);
    c.check(b.graph().entities.size() == before && std::fabs(entity(b, ids[0])->position.x - x0.x) < 1e-6f, "one Ctrl+Z takes the whole clone back");
}

void testCloneInstances(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    const std::vector<uint32_t> ids = cubes(b, 1, true);
    b.select(ids[0]);
    pump(100);
    dragArrowX(v, 0.3f, Qt::ShiftModifier | Qt::ControlModifier);
    answer(w, 3, 1);
    std::vector<uint32_t> copies;
    for (const Entity& e : b.graph().entities)
        if (e.instanceOf == ids[0]) copies.push_back(e.id);
    c.check(copies.size() == 3, "Экземпляр x3: three instances of the cube");
    b.editEntity(ids[0], [](Entity& e) { e.rigid.density = 1234; });
    const bool follow = std::all_of(copies.begin(), copies.end(), [&](uint32_t id) {
        return resolveInstance(b.graph(), *entity(b, id)).rigid.density == 1234.0f;
    });
    b.select(copies.empty() ? 0 : copies.back());
    pump(60);
    auto* density = b.inspectorPanel()->findChild<QDoubleSpinBox*>("rigidDensity");
    std::printf("  the master's density 1234: an instance's inspector shows %.0f\n", density ? density->value() : -1.0);
    c.check(follow && density && density->value() == 1234.0, "the master's new density shows in all three instances");
    b.select(copies.empty() ? 0 : copies.front());
    if (density) density->setValue(777); // typed for an instance: it goes to the master
    pump(40);
    c.check(entity(b, ids[0])->rigid.density == 777.0f, "the density typed for an instance changes its master (and so all)");
}

void testCloneArray(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    const std::vector<uint32_t> ids = cubes(b, 1, true);
    b.select(ids[0]);
    pump(100);
    dragArrowX(v, 0.3f, Qt::ShiftModifier | Qt::ControlModifier);
    answer(w, 4, 2);
    const auto& arrays = b.graph().arrays;
    c.check(arrays.size() == 1 && arrays[0].count[0] == 5 && arrays[0].templateId == ids[0] && !entity(b, ids[0])->visible,
            "Массив via clone: one array of 5, the original its hidden template");
    if (!dir.isEmpty() && !arrays.empty()) {
        b.select(arrays[0].id);
        b.frameAll();
        pump(250);
        windowShot(w).save(dir + "/array-card.png");
    }
    const uint32_t array = arrays.empty() ? 0 : arrays[0].id;
    b.play();
    const bool ran = waitFrames(v, 5, 20000);
    int copies = 0;
    const size_t bodies = v->snapshot() ? v->snapshot()->bodies.size() : 0;
    for (size_t i = 0; i < bodies; ++i) copies += b.entityOfBody(int(i)) == array;
    std::printf("  playing: %zu bodies, %d of them the array's\n", bodies, copies);
    c.check(ran && copies == 5, "▶: the array plays as 5 bodies");
    b.stop();
    pump(100);
}

void testMultiClone(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    const std::vector<uint32_t> ids = cubes(b, 3);
    b.selectMany(ids);
    pump(100);
    const size_t before = b.graph().entities.size();
    dragArrowX(v, 0.2f, Qt::ShiftModifier | Qt::ControlModifier);
    answer(w, 2, 0);
    c.check(b.graph().entities.size() == before + 6 && b.selection().size() == 6, "3 selected, Shift-clone ×2: 6 new cubes, selected");
}

// Ctrl+G, then the X arrow moves the group: both members move by the same step; a click on a member
// selects the group, a second click the member; Ctrl+Z takes the move back.
void testGroups(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    const std::vector<uint32_t> ids = cubes(b, 3);
    b.selectMany({ids[0], ids[1]});
    act(w, "actionGroup");
    const auto& groups = b.graph().groups;
    const bool grouped = groups.size() == 1 && entity(b, ids[0])->parent == groups[0].id && entity(b, ids[1])->parent == groups[0].id;
    c.check(grouped && b.selectedId() == groups[0].id, "Ctrl+G: one group holding both, the group selected");
    if (!grouped) return;
    const uint32_t group = groups[0].id;
    if (auto* tabs = w.findChild<QTabWidget*>("inspectorTabs"); tabs && !dir.isEmpty()) {
        tabs->setCurrentIndex(0); // the scene list: the group holds its two members
        pump(80);
        windowShot(w).save(dir + "/groups-tree.png");
    }
    Vector3 p0, p1;
    Quaternion q;
    worldPose(b.graph(), *entity(b, ids[0]), p0, q);
    trigger(w, "toolMove");
    pump(100);
    dragArrowX(v, 0.4f, Qt::ControlModifier);
    worldPose(b.graph(), *entity(b, ids[0]), p1, q);
    std::printf("  the group dragged 0.4 m: a member moved %.4f m\n", double(p1.x - p0.x));
    c.check(std::fabs(p1.x - p0.x - 0.4f) < 1e-4f, "the gizmo moves the group, and its members with it");
    b.undoAction()->trigger();
    pump(60);
    worldPose(b.graph(), *entity(b, ids[0]), p1, q);
    c.check(std::fabs(p1.x - p0.x) < 1e-5f, "Ctrl+Z puts the group back");
    trigger(w, "toolSelect");
    b.select(0);
    const QPointF member = toPoint(v->gizmoView().project(p0));
    sendMouse(v, QEvent::MouseButtonPress, member, Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, member, Qt::LeftButton, Qt::NoButton);
    pump(40);
    const uint32_t first = b.selectedId();
    sendMouse(v, QEvent::MouseButtonPress, member, Qt::LeftButton, Qt::LeftButton);
    sendMouse(v, QEvent::MouseButtonRelease, member, Qt::LeftButton, Qt::NoButton);
    pump(40);
    c.check(first == group && b.selectedId() == ids[0], "a click on a member selects the group, a second click the member");
    trigger(w, "toolMove");
}

void testMultiEdit(Checker& c, QMainWindow& w, SceneBuilder& b, const QString& dir) {
    const std::vector<uint32_t> ids = cubes(b, 3);
    b.selectMany(ids);
    clickRole(w, RoleIcon::Rigid); // one click: all three get the component
    pump(60);
    const bool allRigid = std::all_of(ids.begin(), ids.end(), [&](uint32_t id) { return entity(b, id)->rigid.enabled; });
    c.check(allRigid, "three selected, «Твёрдое»: all three get the rigid body");
    for (int i = 0; i < 3; ++i) b.editEntity(ids[size_t(i)], [i](Entity& e) { e.rigid.density = 500.0f + 100.0f * float(i); });
    b.selectMany(ids);
    pump(60);
    auto* density = b.inspectorPanel()->findChild<QDoubleSpinBox*>("rigidDensity");
    const bool mixed = density && density->text() == "—";
    c.check(mixed, "different densities show «—»");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/multi-edit.png");
    const float friction = entity(b, ids[0])->rigid.friction;
    pump(750); // typing merges with an edit of the last 0.7 s into one undo step: a person is slower than the script
    if (density) density->setValue(900);
    pump(40);
    const bool all = std::all_of(ids.begin(), ids.end(), [&](uint32_t id) { return entity(b, id)->rigid.density == 900.0f; });
    c.check(all && entity(b, ids[0])->rigid.friction == friction, "900 typed once: all three have it, the other fields untouched");
    b.undoAction()->trigger();
    pump(60);
    c.check(entity(b, ids[0])->rigid.density == 500 && entity(b, ids[1])->rigid.density == 600 && entity(b, ids[2])->rigid.density == 700,
            "Ctrl+Z gives each its own density back");
}

} // namespace

int runManyTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    auto copies = [](int n) { return ruPlural(n, "копия", "копии", "копий"); };
    c.check(copies(1) == "1 копия" && copies(2) == "2 копии" && copies(5) == "5 копий" && copies(11) == "11 копий" &&
                copies(14) == "14 копий" && copies(21) == "21 копия" && copies(104) == "104 копии" && copies(112) == "112 копий",
            "counts agree with their nouns: 1 копия, 2 копии, 5 копий, 11 копий, 21 копия");
    testShiftClone(c, w, b, v, shotsDir);
    testCloneInstances(c, w, b, v);
    testCloneArray(c, w, b, v, shotsDir);
    testMultiClone(c, w, b, v);
    testGroups(c, w, b, v, shotsDir);
    testMultiEdit(c, w, b, shotsDir);
    return c.failures;
}
