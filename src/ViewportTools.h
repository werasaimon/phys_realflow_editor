#pragma once
// Mouse interaction strategies for the Viewport (Strategy pattern): the viewport forwards mouse
// events to the active tool, so new tools (probe, brush, picking...) plug in without touching it.

#include "Gizmo.h"
#include "OrbitCamera.h"

#include <QElapsedTimer>
#include <QPoint>

class QMouseEvent;
class Viewport;

class ViewportTool {
public:
    virtual ~ViewportTool() = default;
    virtual void press(Viewport&, QMouseEvent*) {}
    virtual void move(Viewport&, QMouseEvent*) {}
    virtual void release(Viewport&, QMouseEvent*) {}
};

// Middle button (or LMB where no brush applies) orbits; Shift+middle or right button pans.
class OrbitTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;

private:
    QPoint last_;
};

// LMB grabs a rigid body with a mouse joint: the cursor ray picks the body (any convex shape),
// the hit point becomes the anchor, and dragging moves the target on the plane through the anchor
// facing the camera.
class GrabTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;

private:
    bool grabbing_ = false;
    rf::Vector3 planePoint_, planeNormal_;
    OrbitTool fallback_; // empty space / other buttons move the camera
};

// Edit mode (the scene builder, nothing simulated): LMB on a gizmo handle drags it; Shift+LMB on an
// object slides it across the floor; a click without dragging selects what is under the cursor
// (again on the same spot: the object behind it; empty space: nothing); LMB dragged over empty
// space, the wheel button and RMB move the camera. With no button pressed the handle or object
// under the mouse is highlighted. During a keyboard transform the left button confirms it and the
// right one cancels it.
class EditTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;

private:
    void hover(Viewport& v, QMouseEvent* e);
    bool startFloorDrag(Viewport& v, QMouseEvent* e);

    GizmoHandle pending_ = GizmoHandle::None; // a handle pressed; the drag starts after 3 px
    bool floorDrag_ = false;                  // Shift+drag of an object (Shift is not "fine" then)
    bool clickPending_ = false, rightClickPending_ = false;
    QPointF pressPos_, rightPressPos_;
    rf::Vector2 virtualMouse_, lastMouse_;    // Shift slows a handle drag to a tenth of the mouse
    OrbitTool orbit_;
};

// LMB drag injects a disturbance into the gas: the cursor ray is unprojected into 3D, intersected
// with the slice plane (or the middle of the domain) and the hit point's motion gives the velocity.
class DisturbTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;

private:
    void stroke(Viewport& v, QMouseEvent* e, bool first);

    bool active_ = false;
    rf::Vector3 prevHit_;
    QElapsedTimer clock_;
    OrbitTool fallback_; // wheel/right button still move the camera during a stroke
};
