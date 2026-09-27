#include "ViewportTools.h"

#include "Viewport.h"

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

void MoveTool::press(Viewport& v, QMouseEvent* e) {
    int body;
    rf::Vector3 hit;
    if (e->button() != Qt::LeftButton || !v.pickAnyBody(v.camera().screenRay(e->position(), v.size()), body, hit)) return;
    moving_ = true;
    planeY_ = hit.y;
    emit v.moveStarted(body, hit);
}

void MoveTool::move(Viewport& v, QMouseEvent* e) {
    if (!moving_) return;
    const Ray ray = v.camera().screenRay(e->position(), v.size());
    if (std::fabs(ray.dir.y) < 1e-4f) return; // looking along the floor: no crossing point
    const float t = (planeY_ - ray.origin.y) / ray.dir.y;
    if (t > 0) emit v.moveDragged(ray.at(t));
}

void MoveTool::release(Viewport& v, QMouseEvent* e) {
    if (!moving_ || e->button() != Qt::LeftButton) return;
    moving_ = false;
    emit v.moveFinished();
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
