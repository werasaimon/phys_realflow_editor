// The scene builder's self-test (see SelfTest.h). Each check prints one line, as rf_tests does.
#include "SelfTest.h"

#include "RoleBar.h"
#include "SceneBuilder.h"

#include <QAction>
#include <QMainWindow>
#include <QToolButton>

#include <cstdio>
#include <set>

using namespace rf;

namespace {

struct Checker {
    int failures = 0;
    void check(bool ok, const char* what) {
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
        failures += ok ? 0 : 1;
    }
};

// Presses the role button with this label (the role bar's own button, as a mouse click would).
void clickRole(QMainWindow& w, RoleIcon role) {
    for (QToolButton* b : w.findChildren<QToolButton*>("roleButton"))
        if (b->text() == roleName(role)) b->click();
}

const Entity& last(const SceneBuilder& b) { return b.graph().entities.back(); }

bool idsUnique(const SceneGraph& g) {
    std::set<uint32_t> ids;
    for (const Entity& e : g.entities)
        if (e.id == 0 || !ids.insert(e.id).second) return false;
    return true;
}

} // namespace

int runBuilderSelfTest(QMainWindow& w) {
    Checker c;
    auto* b = w.findChild<SceneBuilder*>();
    if (!b) {
        c.check(false, "the window has a scene builder");
        return c.failures;
    }
    b->newScene();
    c.check(b->graph().entities.size() == 1 && b->graph().entities[0].locked, "a new scene is a locked floor");
    b->createActions()[0]->trigger(); // Куб
    const Entity cube = last(*b);
    c.check(b->graph().entities.size() == 2 && cube.shape == ShapeKind::Box && cube.rigid.enabled && cube.position.y == 1.0f,
            "the cube button makes a rigid cube 1 m above the floor");
    clickRole(w, RoleIcon::Magnet);
    c.check(last(*b).magnet.enabled && last(*b).rigid.enabled, "magnet: added on top of rigid");
    clickRole(w, RoleIcon::Smoke);
    c.check(last(*b).emitter.enabled && b->graph().world.gas, "smoke: the emitter is on and the gas switched itself on");
    clickRole(w, RoleIcon::Cloth);
    c.check(last(*b).cloth.enabled && !last(*b).rigid.enabled, "cloth replaces rigid: made of one thing at a time");
    b->undoAction()->trigger();
    c.check(!last(*b).cloth.enabled && last(*b).rigid.enabled, "undo brings the rigid cube back");
    b->redoAction()->trigger();
    c.check(last(*b).cloth.enabled, "redo makes it cloth again");
    b->createActions()[4]->trigger(); // Плоскость
    clickRole(w, RoleIcon::Cloth);
    c.check(last(*b).cloth.pinnedEdges == 16 && last(*b).position.y == 1.2f, "cloth on a plane hangs itself up at 1.2 m");
    const uint32_t highest = last(*b).id;
    while (b->undoAction()->isEnabled()) b->undoAction()->trigger();
    c.check(b->graph().entities.size() == 1, "undo all the way returns to the floor");
    b->createActions()[1]->trigger(); // Сфера
    c.check(last(*b).id > highest && idsUnique(b->graph()), "ids are never reused within a session");
    std::printf(c.failures ? "%d FAILURE(S)\n" : "ALL PASSED\n", c.failures);
    return c.failures;
}
