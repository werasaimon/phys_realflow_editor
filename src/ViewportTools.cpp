// What the mouse does in the view (see ViewportTools.h): the camera tool (orbit, pan, zoom, look
// around), the grab tool of play mode (a mouse joint on a body or a particle) and the edit tool
// (select, box select, gizmo drags, Shift-clone, keyboard transforms).
#include "ViewportTools.h"

#include "Viewport.h"

#include <QLineF>
#include <QMouseEvent>

static rf::Vector2 pixel(const QMouseEvent* e) { return rf::Vector2(float(e->position().x()), float(e->position().y())); }

// ---------------------------------------------------------------------------
// The camera
// ---------------------------------------------------------------------------
void CameraTool::press(Viewport& v, QMouseEvent* e) {
    last_ = e->pos();
    move_ = v.cameraMoveFor(e->button(), e->modifiers());
    if (move_ == CameraMove::Orbit) pivot_ = v.pointUnderCursor(e->position());
    if (move_ == CameraMove::Dolly) pivot_ = v.camera().target();
    v.setOrbitPivot(pivot_, move_ == CameraMove::Orbit && !v.flying()); // the mark on the point it turns around
}

void CameraTool::move(Viewport& v, QMouseEvent* e) {
    const QPoint d = e->pos() - last_;
    last_ = e->pos();
    CameraMove m = move_;
    if (v.flying() && (m == CameraMove::Orbit || m == CameraMove::Look)) m = CameraMove::Look; // W A S D held: look around
    OrbitCamera& cam = v.camera();
    switch (m) {
    case CameraMove::Orbit: cam.orbitAround(pivot_, float(d.x()), float(d.y())); break;
    case CameraMove::Look: cam.orbitAround(cam.eye(), float(d.x()), float(d.y())); break;
    case CameraMove::Pan: cam.pan(float(d.x()), float(d.y())); break;
    case CameraMove::Dolly: cam.zoomToward(pivot_, float(-d.y()) / 40.0f); break;
    case CameraMove::None: return;
    }
    v.update();
}

void CameraTool::release(Viewport& v, QMouseEvent* e) {
    if (e->buttons() != Qt::NoButton) return;
    move_ = CameraMove::None;
    v.setOrbitPivot(pivot_, false);
}

// ---------------------------------------------------------------------------
// Play mode: grab a body
// ---------------------------------------------------------------------------
void GrabTool::press(Viewport& v, QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && !(e->modifiers() & Qt::AltModifier)) {
        Ray ray = v.camera().screenRay(e->position(), v.size());
        int body;
        rf::Vector3 hit;
        const bool onBody = v.pickBody(ray, body, hit);
        if (onBody || v.pickParticle(ray, hit)) { // a rigid body, or a cloth / soft body / liquid particle
            const QVector3D f = v.camera().forward();
            planeNormal_ = rf::Vector3(f.x(), f.y(), f.z());
            planePoint_ = hit;
            grabbing_ = true;
            if (onBody) emit v.grabStarted(body, hit);
            else emit v.particleGrabStarted(hit);
            return;
        }
    }
    camera_.press(v, e);
}

void GrabTool::move(Viewport& v, QMouseEvent* e) {
    if (!grabbing_) {
        camera_.move(v, e);
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
    camera_.release(v, e);
}

// ---------------------------------------------------------------------------
// Edit mode
// ---------------------------------------------------------------------------
void EditTool::press(Viewport& v, QMouseEvent* e) {
    if (pressTransform(v, e)) return;
    const CameraMove cam = v.cameraMoveFor(e->button(), e->modifiers());
    if (e->button() == Qt::RightButton && !(e->modifiers() & Qt::AltModifier)) { // a click (no drag) opens the menu
        rightClickPending_ = true;
        rightPressPos_ = e->position();
    }
    if (cam != CameraMove::None || e->button() != Qt::LeftButton) {
        camera_.press(v, e);
        return;
    }
    pressLeft(v, e);
}

// A keyboard transform: left confirms, right cancels. A handle drag: the right button puts it back.
bool EditTool::pressTransform(Viewport& v, QMouseEvent* e) {
    if (v.gizmo().modal()) {
        if (e->button() == Qt::LeftButton) v.finishModal();
        else if (e->button() == Qt::RightButton) v.cancelModal();
        return true;
    }
    if (v.gizmo().dragging()) {
        if (e->button() == Qt::RightButton) v.cancelGizmoDrag();
        return true;
    }
    return false;
}

void EditTool::pressLeft(Viewport& v, QMouseEvent* e) {
    pressPos_ = boxEnd_ = e->position();
    pressShift_ = e->modifiers() & Qt::ShiftModifier;
    if (v.gizmoShown()) {
        pending_ = v.gizmo().hitTest(v.gizmoView(), pixel(e));
        if (pending_ != GizmoHandle::None) return; // the drag starts once the mouse has moved 3 px
    }
    pressedEntity_ = v.pickEntity(v.camera().screenRay(e->position(), v.size()));
    clickPending_ = true;
}

void EditTool::startFloorDrag(Viewport& v, QMouseEvent* e) {
    const rf::Vector2 press(float(pressPos_.x()), float(pressPos_.y()));
    rf::Vector3 hit;
    if (!v.pickEntity(v.camera().screenRay(pressPos_, v.size()), &hit)) return;
    emit v.entityClicked(pressedEntity_); // the builder selects it and puts the gizmo on it
    v.gizmo().beginFloorDrag(v.gizmoView(), press, hit);
    floorDrag_ = true;
    virtualMouse_ = lastMouse_ = press;
    emit v.gizmoStarted();
    moveGizmo(v, e);
}

// The left button went 4 px from where it was pressed: on an object with Shift a floor slide, from
// empty space the selection box.
void EditTool::startLeftDrag(Viewport& v, QMouseEvent* e) {
    clickPending_ = false;
    if (pressedEntity_ && (e->modifiers() & Qt::ShiftModifier)) startFloorDrag(v, e);
    else if (!pressedEntity_) box_ = true;
}

bool EditTool::moveGizmo(Viewport& v, QMouseEvent* e) {
    const bool snap = e->modifiers() & Qt::ControlModifier;
    if (v.gizmo().modal()) {
        emit v.gizmoMoved(v.gizmo().drag(v.gizmoView(), pixel(e), snap));
        v.update();
        return true;
    }
    if (pending_ != GizmoHandle::None) { // a pressed handle: a click does not nudge the object
        if (QLineF(e->position(), pressPos_).length() < 3) return true;
        const rf::Vector2 press(float(pressPos_.x()), float(pressPos_.y()));
        v.gizmo().begin(pending_, v.gizmoView(), press);
        pending_ = GizmoHandle::None;
        floorDrag_ = false;
        virtualMouse_ = lastMouse_ = press;
        cloning_ = cloneShiftHeld_ = pressShift_; // Shift at the start: a clone, as in 3ds Max
        emit v.gizmoStarted();
        if (cloning_) emit v.cloneDragStarted();
    }
    if (!v.gizmo().dragging()) return false;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    if (!shift) cloneShiftHeld_ = false; // let go: pressed again, it means "finer"
    const float speed = shift && !floorDrag_ && !cloneShiftHeld_ ? 0.1f : 1.0f;
    virtualMouse_ += (pixel(e) - lastMouse_) * speed;
    lastMouse_ = pixel(e);
    emit v.gizmoMoved(v.gizmo().drag(v.gizmoView(), virtualMouse_, snap));
    v.update();
    return true;
}

void EditTool::move(Viewport& v, QMouseEvent* e) {
    if (moveGizmo(v, e)) return;
    if (rightClickPending_ && (QLineF(e->position(), rightPressPos_).length() > 4 || v.flying())) rightClickPending_ = false;
    if (box_) {
        boxEnd_ = e->position();
        v.update();
        return;
    }
    if (clickPending_ && (e->buttons() & Qt::LeftButton)) {
        if (QLineF(e->position(), pressPos_).length() > 4) startLeftDrag(v, e);
        return;
    }
    if (e->buttons() != Qt::NoButton) {
        camera_.move(v, e);
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
    camera_.release(v, e);
    if (e->button() == Qt::RightButton) {
        if (rightClickPending_ && !v.flewThisPress()) emit v.editContextMenu(e->position());
        rightClickPending_ = false;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (pending_ != GizmoHandle::None) { // pressed and released on a handle without moving
        pending_ = GizmoHandle::None;
        return;
    }
    if (v.gizmo().dragging() && !v.gizmo().modal()) {
        v.gizmo().end();
        floorDrag_ = false;
        const bool cloned = cloning_;
        cloning_ = false;
        emit v.gizmoFinished();
        if (cloned) emit v.cloneDragFinished(e->position()); // the popover opens at the cursor
        v.update();
        return;
    }
    if (box_) {
        box_ = false;
        emit v.boxSelected(boxRect(), e->modifiers());
        v.update();
        return;
    }
    const bool toggle = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
    if (clickPending_ && toggle && pressedEntity_) emit v.entityToggled(pressedEntity_);
    else if (clickPending_ && !toggle) v.clickSelect(e->position(), false);
    clickPending_ = false;
}

// ---------------------------------------------------------------------------
// Play mode in a gas: stir it
// ---------------------------------------------------------------------------
void DisturbTool::press(Viewport& v, QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || (e->modifiers() & Qt::AltModifier)) {
        camera_.press(v, e);
        return;
    }
    stroke(v, e, true);
}

void DisturbTool::move(Viewport& v, QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton) || !active_) {
        camera_.move(v, e);
        return;
    }
    stroke(v, e, false);
}

void DisturbTool::release(Viewport& v, QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) active_ = false;
    camera_.release(v, e);
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
