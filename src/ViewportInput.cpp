// The viewport's mouse and keys (see Viewport.h and docs/controls.md): the navigation cube, the
// tools, the camera flight on W A S D, the keys of a transform in progress, the edit mode's picking
// and the line of the status bar that says what the buttons and keys do right now.
#include "Viewport.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using rf::Vector3;

namespace {
bool isFlyKey(int k) { return k == Qt::Key_W || k == Qt::Key_A || k == Qt::Key_S || k == Qt::Key_D || k == Qt::Key_Q || k == Qt::Key_E; }
} // namespace

// ---------------------------------------------------------------------------
// The camera
// ---------------------------------------------------------------------------
void Viewport::frameScene() {
    if (focusBox_.valid()) {
        camera_.frame(focusBox_, 40.0f, 24.0f);
        update();
        return;
    }
    if (!snap_ || !snap_->domain.valid()) return;
    // Voxel/vector scenes look straight at the Z slice; others use a 3/4 view.
    if (snap_->vis.gridDisplay > 0 || snap_->vis.vectorDisplay > 0) camera_.frame(snap_->domain, 90.0f, 12.0f);
    else camera_.frame(snap_->domain, snap_->mode == rf::SimMode::WindTunnel ? 60.0f : 55.0f, 22.0f);
    update();
}

// Looks along ball k of the navigation cube (+X, -X, +Y, -Y, +Z, -Z) at the scene: +Y is the top view.
void Viewport::viewAlong(int k) {
    camera_.lookAlong(-NavCube::axis(k));
    update();
    updateHint();
}

// The point the camera turns around and zooms towards: what the cursor is on (an object, a body),
// else the floor, else the depth of the current target.
QVector3D Viewport::pointUnderCursor(const QPointF& pos) const {
    const Ray ray = camera_.screenRay(pos, size());
    const auto q = [](const Vector3& p) { return QVector3D(p.x, p.y, p.z); };
    if (editMode_) {
        const std::vector<PickHit> hits = pickAll(ray);
        if (!hits.empty()) return q(hits.front().point);
    } else {
        int body;
        Vector3 hit;
        if (pickAnyBody(ray, body, hit)) return q(hit);
    }
    const float toFloor = intersectAxisPlane(ray, 1, 0.0f);
    if (toFloor > 0.0f && toFloor < 20.0f * camera_.distance()) return q(ray.at(toFloor));
    const QVector3D f = camera_.forward(), o = q(ray.origin), d = q(ray.dir);
    const float t = QVector3D::dotProduct(camera_.target() - o, f) / std::max(1e-4f, QVector3D::dotProduct(d, f));
    return o + d * t;
}

ViewportTool& Viewport::toolFor(Qt::MouseButton button, const QPointF& pos) {
    if (editMode_) return editTool_; // the scene builder's edit mode: select, gizmo, camera
    // LMB is the disturbance ray wherever there is gas to disturb - unless it hits a rigid body,
    // which it then grabs.
    bool gas = snap_ && snap_->mode == rf::SimMode::WindTunnel;
    if (button == Qt::LeftButton && gas) {
        int body;
        Vector3 hit;
        const Ray ray = camera_.screenRay(pos, size());
        if (!snap_->bodies.empty() && pickBody(ray, body, hit)) return grabTool_;
        if (pickParticle(ray, hit)) return grabTool_; // cloth / soft body
        return disturbTool_;
    }
    if (button == Qt::LeftButton && snap_) return grabTool_; // bodies, cloth, soft bodies, liquid particles
    return cameraTool_;
}

// ---------------------------------------------------------------------------
// The mouse
// ---------------------------------------------------------------------------
// The navigation cube takes a left press on it: a click on a ball looks along that axis, a drag orbits.
bool Viewport::navPress(QMouseEvent* e) {
    if (grabbed_ || e->button() != Qt::LeftButton || !overlayVisible_) return false;
    const int h = navCube_.hit(e->position(), viewMatrix(), size());
    if (h == NavCube::kOutside) return false;
    navPressed_ = true;
    navDragged_ = false;
    navPressHit_ = h;
    navLast_ = e->pos();
    return true;
}

void Viewport::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason); // the keys (W A S D, X Y Z during a drag) come here
    if (navPress(e)) return;
    if (e->button() == Qt::RightButton) rightHeld_ = true;
    if (e->button() == Qt::LeftButton) labPress_ = e->position(), labPressed_ = true; // a Laboratory click?
    const bool alt = e->modifiers() & Qt::AltModifier;
    if (!editMode_ && !grabbed_ && e->button() == Qt::LeftButton && !alt && !(e->modifiers() & Qt::ShiftModifier)) {
        int body;
        Vector3 hit;
        if (pickAnyBody(camera_.screenRay(e->position(), size()), body, hit)) emit bodyClicked(body);
    }
    if (!grabbed_) grabbed_ = &toolFor(e->button(), e->position());
    grabbed_->press(*this, e);
    updateHint();
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
    lastMouse_ = e->position();
    if (navPressed_) {
        const QPoint d = e->pos() - navLast_;
        if (!navDragged_ && QLineF(e->position(), QPointF(navLast_)).length() < 3) return;
        navDragged_ = true;
        navLast_ = e->pos();
        camera_.rotate(float(d.x()), float(d.y()));
        update();
        return;
    }
    const int hover = overlayVisible_ ? navCube_.hit(e->position(), viewMatrix(), size()) : NavCube::kOutside;
    if (hover != navHover_) {
        navHover_ = hover;
        update();
    }
    if (grabbed_) grabbed_->move(*this, e);
    else if (editMode_) editTool_.move(*this, e); // hover, or a keyboard transform following the mouse
    updateHint();
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    if (navPressed_ && e->button() == Qt::LeftButton) {
        if (!navDragged_ && navPressHit_ >= 0) viewAlong(navPressHit_);
        navPressed_ = false;
        return;
    }
    if (grabbed_) grabbed_->release(*this, e);
    if (e->button() == Qt::RightButton) stopFlying();
    if (e->buttons() == Qt::NoButton) grabbed_ = nullptr;
    if (e->button() == Qt::LeftButton && labPressed_) { // a press and a release within 4 px: a click
        labPressed_ = false;
        if (labInspect_ && QLineF(labPress_, e->position()).length() < 4) emit labClicked(e->position());
    }
    update();
    updateHint();
}

void Viewport::wheelEvent(QWheelEvent* e) {
    camera_.zoomToward(pointUnderCursor(e->position()), e->angleDelta().y() / 120.0f);
    update();
}

// A double click is F: the selected object framed (on empty space the first click selected nothing,
// so the whole scene). Not on the navigation cube or a gizmo handle.
void Viewport::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || navCube_.hit(e->position(), viewMatrix(), size()) != NavCube::kOutside) return;
    if (!editMode_) return frameScene();
    if (gizmoShown() && gizmo_.hitTest(gizmoView(), rf::Vector2(float(e->position().x()), float(e->position().y()))) != GizmoHandle::None) return;
    emit frameSelectedRequested();
}

void Viewport::leaveEvent(QEvent*) {
    if (navHover_ != NavCube::kOutside) navHover_ = NavCube::kOutside, update();
}

// ---------------------------------------------------------------------------
// The keys
// ---------------------------------------------------------------------------
void Viewport::keyPressEvent(QKeyEvent* e) {
    if (gizmo_.modal() && modalKey(e)) return updateHint();
    if (gizmo_.dragging()) return dragKey(e), updateHint(); // a handle drag: Esc cancels, X Y Z lock an axis
    if (flyKey(e, true)) return;
    const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    if (editMode_ && blenderKeys(scheme_) && mods == Qt::NoModifier) { // Blender: G move, R turn, S scale
        if (e->key() == Qt::Key_G) return startModal(GizmoMode::Translate);
        if (e->key() == Qt::Key_R) return startModal(GizmoMode::Rotate);
        if (e->key() == Qt::Key_S) return startModal(GizmoMode::Scale);
    }
    QOpenGLWidget::keyPressEvent(e);
}

void Viewport::keyReleaseEvent(QKeyEvent* e) {
    if (!flyKey(e, false)) QOpenGLWidget::keyReleaseEvent(e);
}

void Viewport::focusOutEvent(QFocusEvent* e) {
    stopFlying();
    QOpenGLWidget::focusOutEvent(e);
}

// During a handle drag: Esc puts the object back; X / Y / Z switch the drag to that axis, Shift+X /
// Y / Z to the plane across it (Blender's axis locking, docs/controls.md).
void Viewport::dragKey(QKeyEvent* e) {
    const int k = e->key();
    if (k == Qt::Key_Escape) return cancelGizmoDrag();
    if (k != Qt::Key_X && k != Qt::Key_Y && k != Qt::Key_Z) return;
    const int axis = k - Qt::Key_X;
    const GizmoHandle axes[3] = {GizmoHandle::AxisX, GizmoHandle::AxisY, GizmoHandle::AxisZ};
    const GizmoHandle planes[3] = {GizmoHandle::PlaneYZ, GizmoHandle::PlaneXZ, GizmoHandle::PlaneXY};
    gizmo_.switchHandle((e->modifiers() & Qt::ShiftModifier) ? planes[axis] : axes[axis], gizmoView());
    const bool snap = e->modifiers() & Qt::ControlModifier;
    emit gizmoMoved(gizmo_.drag(gizmoView(), rf::Vector2(float(lastMouse_.x()), float(lastMouse_.y())), snap));
    update();
}

// W A S D while the right button is held: fly. Returns true when the key was a flight key.
bool Viewport::flyKey(QKeyEvent* e, bool down) {
    if (!rightHeld_ || !rightButtonFlies(scheme_) || !isFlyKey(e->key())) return false;
    if (e->isAutoRepeat()) return true;
    if (down) flyKeys_.insert(e->key());
    else flyKeys_.erase(e->key());
    if (down && !flyTimer_->isActive()) {
        flyClock_.start();
        flyTimer_->start();
    }
    flew_ = flew_ || down;
    updateHint();
    return true;
}

// One step of the flight: forward / back (W / S), sideways (A / D), down / up (Q / E); Shift is 4x.
void Viewport::flyTick() {
    if (flyKeys_.empty() || !rightHeld_) return flyTimer_->stop();
    const float dt = std::min(0.05f, float(flyClock_.restart()) * 1e-3f);
    const bool fast = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
    const float speed = std::max(1.0f, 0.6f * camera_.distance()) * (fast ? 4.0f : 1.0f);
    const auto held = [this](int k) { return flyKeys_.count(k) ? 1.0f : 0.0f; };
    const QVector3D step = camera_.forward() * (held(Qt::Key_W) - held(Qt::Key_S)) + camera_.right() * (held(Qt::Key_D) - held(Qt::Key_A)) +
                           QVector3D(0, 1, 0) * (held(Qt::Key_E) - held(Qt::Key_Q));
    camera_.translate(step * (speed * dt));
    update();
}

void Viewport::stopFlying() {
    rightHeld_ = false;
    flyKeys_.clear();
    flyTimer_->stop();
    flew_ = false;
}

bool Viewport::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride) {
        auto* k = static_cast<QKeyEvent*>(e);
        const bool flight = rightHeld_ && rightButtonFlies(scheme_) && isFlyKey(k->key());
        if (gizmo_.dragging() || flight) { // X, Esc, digits, W A S D... belong to the drag, not to the window's shortcuts
            e->accept();
            return true;
        }
    }
    return QOpenGLWidget::event(e);
}

// ---------------------------------------------------------------------------
// Edit mode: picking, the gizmo, keyboard transforms
// ---------------------------------------------------------------------------
void Viewport::setEditMode(bool on) {
    if (!on) cancelModal();
    editMode_ = on;
    setMouseTracking(true); // the navigation cube lights up under the mouse in every mode
    if (!on) {
        gizmo_.setHover(GizmoHandle::None);
        hoveredEntity_ = 0;
        hoverBodies_.clear();
        ghostBodies_.clear();
    }
    update();
    updateHint();
}

void Viewport::setGizmoTarget(bool visible, const Vector3& position, const rf::Quaternion& rotation) {
    gizmoVisible_ = visible;
    gizmo_.setTarget(position, rotation);
    update();
}

uint32_t Viewport::pickEntity(const Ray& ray, Vector3* hit) const {
    const std::vector<PickHit> hits = pickAll(ray);
    if (hits.empty()) return 0;
    if (hit) *hit = hits.front().point;
    return hits.front().id;
}

void Viewport::setHoveredEntity(uint32_t id) {
    if (id == hoveredEntity_) return;
    hoveredEntity_ = id;
    emit entityHovered(id);
}

// The objects on the ray, nearest first. Clicking the same spot again within 1.5 s (the camera
// unmoved) takes the next one. A hint at the cursor says "2 / 3: name".
uint32_t Viewport::clickSelect(const QPointF& pos, bool next) {
    const std::vector<PickHit> hits = pickAll(camera_.screenRay(pos, size()));
    std::vector<uint32_t> ids;
    for (const PickHit& h : hits) ids.push_back(h.id);
    const bool sameSpot = lastClickTime_.isValid() && lastClickTime_.elapsed() < 1500 && QLineF(pos, lastClickPos_).length() <= 4 &&
                          camera_.view() == lastClickView_;
    int i = 0;
    if (!ids.empty() && ids == lastClickHits_ && lastClickIndex_ >= 0 && (sameSpot || next)) i = (lastClickIndex_ + 1) % int(ids.size());
    else if (next && ids.size() > 1) i = 1;
    lastClickPos_ = pos;
    lastClickTime_.start();
    lastClickView_ = camera_.view();
    lastClickHits_ = ids;
    lastClickIndex_ = ids.empty() ? -1 : i;
    if (ids.size() > 1)
        QToolTip::showText(mapToGlobal(pos.toPoint()) + QPoint(16, 8), QString("%1 / %2: %3").arg(i + 1).arg(ids.size()).arg(QString::fromStdString(hits[size_t(i)].name)), this);
    else
        QToolTip::hideText();
    const uint32_t id = ids.empty() ? 0 : ids[size_t(i)];
    emit entityClicked(id);
    return id;
}

void Viewport::startModal(GizmoMode m) {
    if (!editMode_ || !gizmoVisible_ || gizmo_.dragging()) return;
    const QPointF p = mapFromGlobal(QCursor::pos());
    gizmo_.beginModal(m, gizmoView(), rf::Vector2(float(p.x()), float(p.y())));
    typed_.clear();
    emit gizmoStarted();
    update();
    updateHint();
}

void Viewport::finishModal() {
    if (!gizmo_.modal()) return;
    gizmo_.end();
    typed_.clear();
    emit gizmoFinished();
    update();
    updateHint();
}

void Viewport::cancelModal() {
    if (!gizmo_.modal()) return;
    gizmo_.end();
    typed_.clear();
    emit gizmoCancelled();
    update();
    updateHint();
}

void Viewport::cancelGizmoDrag() {
    if (!gizmo_.dragging() || gizmo_.modal()) return;
    gizmo_.end();
    emit gizmoCancelled();
    update();
    updateHint();
}

void Viewport::modalDrag() {
    const QPointF p = mapFromGlobal(QCursor::pos());
    const bool snap = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    emit gizmoMoved(gizmo_.drag(gizmoView(), rf::Vector2(float(p.x()), float(p.y())), snap));
    update();
}

// During a keyboard transform: X / Y / Z limit it to an axis, digits type the amount, Enter
// confirms, Esc cancels; every other key is swallowed.
bool Viewport::modalKey(QKeyEvent* e) {
    const int k = e->key();
    if (k == Qt::Key_Escape) cancelModal();
    else if (k == Qt::Key_Return || k == Qt::Key_Enter) finishModal();
    else if (k == Qt::Key_X || k == Qt::Key_Y || k == Qt::Key_Z) {
        gizmo_.constrainModal(k - Qt::Key_X, gizmoView());
        modalDrag();
    } else if (k == Qt::Key_Backspace || (e->text().size() == 1 && QString("0123456789.,-").contains(e->text()))) {
        if (k == Qt::Key_Backspace) typed_.chop(1);
        else typed_ += e->text() == "," ? QString(".") : e->text();
        bool ok = false;
        const float value = typed_.toFloat(&ok);
        gizmo_.setTyped(ok, value);
        modalDrag();
    }
    return true;
}

// ---------------------------------------------------------------------------
// What the buttons and keys do right now (the status bar)
// ---------------------------------------------------------------------------
QString Viewport::mouseHint() const {
    if (navPressed_ || navHover_ != NavCube::kOutside) return "Клик по шарику оси: вид вдоль неё (Y — сверху) · тяните: вращать вид";
    if (rightHeld_ && rightButtonFlies(scheme_) && (flew_ || !flyKeys_.empty()))
        return "Полёт: W A S D — вперёд, влево, назад, вправо · Q / E — вниз / вверх · Shift — быстрее · мышь — осмотреться";
    if (!editMode_)
        return "ЛКМ по телу: схватить и тащить · " + cameraHint(scheme_) + " · Пробел: пауза · Esc: стоп";
    if (gizmo_.modal()) return "X / Y / Z: ось (ещё раз — своя) · число: точно · Enter или ЛКМ: принять · Esc или ПКМ: отмена";
    if (gizmo_.dragging() && editTool_.cloning())
        return "Клонирование: отпустите — спросим, сколько копий · X / Y / Z: ось · Ctrl: шаг · Esc или ПКМ: отмена";
    if (gizmo_.dragging()) return "X / Y / Z: только по оси · Shift+X / Y / Z: в плоскости · Ctrl: шаг · Shift: точнее · Esc или ПКМ: отмена";
    if (editTool_.boxSelecting()) return "Отпустите — выбрать всё в рамке · Shift: добавить · Ctrl: убрать";
    if (gizmoShown() && gizmo_.hover() != GizmoHandle::None) {
        const char* verb = gizmo_.mode() == GizmoMode::Rotate ? "вращать" : gizmo_.mode() == GizmoMode::Scale ? "масштабировать" : "двигать";
        return QString("Тяните: %1 · Shift+тяните: клонировать · Ctrl: шаг · Shift во время драга: точнее · X / Y / Z: ось").arg(verb);
    }
    if (hoveredEntity_) return "ЛКМ: выбрать · Shift или Ctrl+ЛКМ: добавить / убрать · двойной клик: показать · " + cameraHint(scheme_);
    return "ЛКМ: выбрать · тяните с пустого места: рамка · " + cameraHint(scheme_) + " · F: показать";
}

void Viewport::updateHint() {
    const QString hint = mouseHint();
    if (hint == lastHint_) return;
    lastHint_ = hint;
    emit hintChanged(hint);
}
