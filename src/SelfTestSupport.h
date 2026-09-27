#pragma once
// What the self-test files share (see SelfTest.h): the checker that prints one line per check, and
// the hands of the test - wait while the window works, press a role button, trigger an action by
// name, send a mouse event to the viewport, wait for simulated frames.
#include "RoleBar.h"
#include "SceneBuilder.h"
#include "Viewport.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QThread>
#include <QToolButton>

#include <cstdio>

namespace selftest {

struct Checker {
    int failures = 0;
    void check(bool ok, const char* what) {
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
        std::fflush(stdout);
        failures += ok ? 0 : 1;
    }
};

// Lets the window work for a while: events, the simulation's snapshots, painting.
inline void pump(int ms) {
    QElapsedTimer t;
    t.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    } while (t.elapsed() < ms);
}

// Presses the role button with this label (the role bar's own button, as a mouse click would).
inline void clickRole(QMainWindow& w, RoleIcon role) {
    for (QToolButton* b : w.findChildren<QToolButton*>("roleButton"))
        if (b->text() == roleName(role)) b->click();
}

inline void trigger(QMainWindow& w, const char* action) {
    if (auto* a = w.findChild<QAction*>(action)) a->trigger();
}

inline const rf::Entity& last(const SceneBuilder& b) { return b.graph().entities.back(); }

inline bool same(const rf::Vector3& a, const rf::Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

inline void sendMouse(Viewport* v, QEvent::Type type, const QPointF& p, Qt::MouseButton button, Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent e(type, p, v->mapToGlobal(p), button, buttons, modifiers);
    QCoreApplication::sendEvent(v, &e);
}

inline QPointF toPoint(const rf::Vector2& p) { return QPointF(p.x, p.y); }

// The whole window as it is on the screen: a widget grab draws the 3D view without its painted
// overlay (the caption, the navigation cube, the selection box), so the view's own frame is laid over it.
inline QImage windowShot(QMainWindow& w) {
    QImage shot = w.grab().toImage();
    if (auto* v = w.findChild<Viewport*>()) {
        QPainter p(&shot);
        p.drawImage(QRect(v->mapTo(&w, QPoint(0, 0)), v->size()), v->grabFramebuffer());
    }
    return shot;
}

// Waits until the viewport shows simulated frame `frames` (false: it did not come in time).
inline bool waitFrames(Viewport* v, uint64_t frames, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        pump(40);
        if (v->snapshot() && v->snapshot()->frame >= frames) return true;
    }
    return false;
}

} // namespace selftest

// The component tests and their screenshots (SelfTestComponents.cpp): the inspector's cards, the
// collider as a component of its own, the first minute. Return the number of failures.
int runComponentTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
// The keys and the mouse of docs/controls.md (SelfTestControls.cpp).
int runControlTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir);
