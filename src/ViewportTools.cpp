#include "ViewportTools.h"

#include "Viewport.h"

#include <QLineF>
#include <QMouseEvent>

void OrbitTool::press(Viewport&, QMouseEvent* e) { last_ = e->pos(); }

void OrbitTool::move(Viewport& v, QMouseEvent* e) {
    QPoint d = e->pos() - last_;
    last_ = e->pos();
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    if ((e->buttons() & Qt::MiddleButton) && !shift) v.camera().rotate(float(d.x()), float(d.y()));
    else if (e->buttons() & (Qt::RightButton | Qt::MiddleButton)) v.camera().pan(float(d.x()), float(d.y()));
    else if (e->buttons() & Qt::LeftButton) v.camera().rotate(float(d.x()), float(d.y()));
    v.update();
}

void GrabTool::press(Viewport& v, QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        Ray ray = v.camera().screenRay(e->position(), v.size());
        int body;
        rf::Vector3 hit;
        if (v.pickBody(ray, body, hit)) {
            QVector3D f = v.camera().forward();
            planeNormal_ = rf::Vector3(f.x(), f.y(), f.z());
            planePoint_ = hit;
            grabbing_ = true;
            emit v.grabStarted(body, hit);
            return;
        }
        if (v.pickParticle(ray, hit)) { // cloth, soft body or liquid particle
            QVector3D f = v.camera().forward();
            planeNormal_ = rf::Vector3(f.x(), f.y(), f.z());
            planePoint_ = hit;
            grabbing_ = true;
            emit v.particleGrabStarted(hit);
            return;
        }
    }
    fallback_.press(v, e);
}

void GrabTool::move(Viewport& v, QMouseEvent* e) {
    if (!grabbing_) {
        fallback_.move(v, e);
        return;
    }
    Ray ray = v.camera().screenRay(e->position(), v.size());
    float denom = rf::dot(ray.dir, planeNormal_);
    if (std::fabs(denom) < 1e-6f) return;
    float t = rf::dot(planePoint_ - ray.origin, planeNormal_) / denom;
    if (t <= 0) return;
    emit v.grabMoved(ray.at(t));
}

void GrabTool::release(Viewport& v, QMouseEvent* e) {
    if (grabbing_ && e->button() == Qt::LeftButton) {
        grabbing_ = false;
        emit v.grabReleased();
    }
}

static rf::Vector2 pixel(const QMouseEvent* e) { return rf::Vector2(float(e->position().x()), float(e->position().y())); }

void EditTool::press(Viewport& v, QMouseEvent* e) {
    if (v.gizmo().modal()) { // a keyboard transform: left confirms, right cancels
        if (e->button() == Qt::LeftButton) v.finishModal();
        else if (e->button() == Qt::RightButton) v.cancelModal();
        return;
    }
    if (v.gizmo().dragging()) { // the right button during a handle drag puts the object back
        if (e->button() == Qt::RightButton) v.cancelGizmoDrag();
        return;
    }
    if (e->button() != Qt::LeftButton) {
        if (e->button() == Qt::RightButton) { // a right click (no drag) opens the context menu
            rightClickPending_ = true;
            rightPressPos_ = e->position();
        }
        orbit_.press(v, e);
        return;
    }
    const bool alt = e->modifiers() & Qt::AltModifier; // Alt+click: the next object on the ray, never a handle
    if (!alt && v.gizmoShown()) {
        pending_ = v.gizmo().hitTest(v.gizmoView(), pixel(e));
        if (pending_ != GizmoHandle::None) { // the drag starts once the mouse has moved 3 px
            pressPos_ = e->position();
            return;
        }
    }
    if ((e->modifiers() & Qt::ShiftModifier) && startFloorDrag(v, e)) return;
    pressPos_ = e->position();
    clickPending_ = true;
    orbit_.press(v, e);
}

// Shift+drag: the object under the cursor is selected and slides on the horizontal plane through
// the point that was hit, which stays under the cursor.
bool EditTool::startFloorDrag(Viewport& v, QMouseEvent* e) {
    rf::Vector3 hit;
    const uint32_t id = v.pickEntity(v.camera().screenRay(e->position(), v.size()), &hit);
    if (!id) return false;
    emit v.entityClicked(id); // the builder selects it and puts the gizmo on it
    v.gizmo().beginFloorDrag(v.gizmoView(), pixel(e), hit);
    floorDrag_ = true;
    virtualMouse_ = lastMouse_ = pixel(e);
    emit v.gizmoStarted();
    return true;
}

void EditTool::move(Viewport& v, QMouseEvent* e) {
    const bool snap = e->modifiers() & Qt::ControlModifier;
    if (v.gizmo().modal()) {
        emit v.gizmoMoved(v.gizmo().drag(v.gizmoView(), pixel(e), snap));
        v.update();
        return;
    }
    if (pending_ != GizmoHandle::None) { // a pressed handle: a click does not nudge the object
        if (QLineF(e->position(), pressPos_).length() < 3) return;
        const rf::Vector2 press(float(pressPos_.x()), float(pressPos_.y()));
        v.gizmo().begin(pending_, v.gizmoView(), press);
        pending_ = GizmoHandle::None;
        floorDrag_ = false;
        virtualMouse_ = lastMouse_ = press;
        emit v.gizmoStarted();
    }
    if (v.gizmo().dragging()) {
        const float speed = (e->modifiers() & Qt::ShiftModifier) && !floorDrag_ ? 0.1f : 1.0f;
        virtualMouse_ += (pixel(e) - lastMouse_) * speed;
        lastMouse_ = pixel(e);
        emit v.gizmoMoved(v.gizmo().drag(v.gizmoView(), virtualMouse_, snap));
        v.update();
        return;
    }
    if (e->buttons() != Qt::NoButton) {
        if (clickPending_ && QLineF(e->position(), pressPos_).length() > 4) clickPending_ = false; // a drag, not a click
        if (rightClickPending_ && QLineF(e->position(), rightPressPos_).length() > 4) rightClickPending_ = false;
        orbit_.move(v, e);
        return;
    }
    hover(v, e);
}

void EditTool::hover(Viewport& v, QMouseEvent* e) {
    const GizmoHandle h = v.gizmoShown() ? v.gizmo().hitTest(v.gizmoView(), pixel(e)) : GizmoHandle::None;
    if (h != v.gizmo().hover()) {
        v.gizmo().setHover(h);
        v.setCursor(h != GizmoHandle::None ? Qt::SizeAllCursor : Qt::ArrowCursor);
        v.update();
    }
    v.setHoveredEntity(h == GizmoHandle::None ? v.pickEntity(v.camera().screenRay(e->position(), v.size())) : 0);
}

void EditTool::release(Viewport& v, QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        if (rightClickPending_) emit v.editContextMenu(e->position());
        rightClickPending_ = false;
        return;
    }
    if (pending_ != GizmoHandle::None) { // pressed and released on a handle without moving
        pending_ = GizmoHandle::None;
        return;
    }
    if (v.gizmo().dragging() && !v.gizmo().modal()) {
        if (e->button() != Qt::LeftButton) return;
        v.gizmo().end();
        floorDrag_ = false;
        emit v.gizmoFinished();
        v.update();
        return;
    }
    if (clickPending_ && e->button() == Qt::LeftButton) v.clickSelect(e->position(), e->modifiers() & Qt::AltModifier);
    clickPending_ = false;
}

void DisturbTool::press(Viewport& v, QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) {
        fallback_.press(v, e);
        return;
    }
    stroke(v, e, true);
}

void DisturbTool::move(Viewport& v, QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) {
        fallback_.move(v, e);
        return;
    }
    if (active_) stroke(v, e, false);
}

void DisturbTool::release(Viewport&, QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) active_ = false;
}

void DisturbTool::stroke(Viewport& v, QMouseEvent* e, bool first) {
    Ray ray = v.camera().screenRay(e->position(), v.size());
    rf::Vector3 hit;
    if (!v.pickPoint(ray, hit)) {
        active_ = false;
        return;
    }
    rf::Vector3 vel(0.0f);
    if (!first && active_) {
        float dt = std::max(clock_.restart() * 1e-3f, 1.0f / 240.0f);
        vel = (hit - prevHit_) / dt;
        const float vmax = 25.0f; // m/s, protects against jumps between frames
        float s = rf::length(vel);
        if (s > vmax) vel *= vmax / s;
    } else {
        clock_.start();
    }
    active_ = true;
    prevHit_ = hit;
    v.showProbe(ray, hit);
    emit v.disturbanceRequested(hit, vel);
}
