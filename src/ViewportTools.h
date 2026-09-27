#pragma once
// Mouse interaction strategies for the Viewport (Strategy pattern): the viewport forwards mouse
// events to the active tool, so new tools (probe, brush, picking...) plug in without touching it.
// Which button moves the camera is the control scheme's business (ControlScheme.h); the tools ask
// the viewport for it, so the left button never moves the camera unless Alt is held.

#include "ControlScheme.h"
#include "Gizmo.h"
#include "OrbitCamera.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QRectF>
#include <QVector3D>

class QMouseEvent;
class Viewport;

class ViewportTool {
public:
    virtual ~ViewportTool() = default;
    virtual void press(Viewport&, QMouseEvent*) {}
    virtual void move(Viewport&, QMouseEvent*) {}
    virtual void release(Viewport&, QMouseEvent*) {}
};

// The camera: what a drag does is decided at the press from the button, the keys held and the
// scheme - orbit around the point under the cursor (it stays where it is on screen), pan, zoom, or
// look around in place. While the right button is held and W A S D fly the camera, a drag looks
// around instead of orbiting (Unity, Unreal).
class CameraTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;
    CameraMove active() const { return move_; }

private:
    QPoint last_;
    CameraMove move_ = CameraMove::None;
    QVector3D pivot_;
};

// Play mode: the left button grabs a rigid body with a mouse joint - the cursor ray picks the body
// (any convex shape), the hit point becomes the anchor, and dragging moves the target on the plane
// through the anchor facing the camera. Other buttons (and Alt+left) move the camera.
class GrabTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;

private:
    bool grabbing_ = false;
    rf::Vector3 planePoint_, planeNormal_;
    CameraTool camera_;
};

// Edit mode (the scene builder, nothing simulated), docs/controls.md:
//   left button - on a gizmo handle: drag it; a click on an object selects it (again on the same
//     spot: the one behind it; Shift or Ctrl + click adds or takes it away); a click on empty space
//     selects nothing; a drag from empty space draws a selection box (Shift adds, Ctrl takes away);
//     Shift + drag of an object slides it across the floor;
//   right button - a click opens the menu, a drag moves the camera (the scheme says how);
//   middle button, Alt + any button - the camera.
// With no button pressed the handle or object under the mouse is highlighted. During a keyboard
// transform the left button confirms it and the right one cancels it.
class EditTool : public ViewportTool {
public:
    void press(Viewport& v, QMouseEvent* e) override;
    void move(Viewport& v, QMouseEvent* e) override;
    void release(Viewport& v, QMouseEvent* e) override;
    bool boxSelecting() const { return box_; }
    QRectF boxRect() const { return QRectF(pressPos_, boxEnd_).normalized(); }

private:
    bool pressTransform(Viewport& v, QMouseEvent* e); // a keyboard transform or a handle drag takes the press
    void pressLeft(Viewport& v, QMouseEvent* e);
    bool moveGizmo(Viewport& v, QMouseEvent* e);      // true: the gizmo took the move
    void startLeftDrag(Viewport& v, QMouseEvent* e);  // a floor slide or the selection box
    void hover(Viewport& v, QMouseEvent* e);
    void startFloorDrag(Viewport& v, QMouseEvent* e);

    GizmoHandle pending_ = GizmoHandle::None; // a handle pressed; the drag starts after 3 px
    bool floorDrag_ = false;                  // Shift+drag of an object (Shift is not "fine" then)
    bool clickPending_ = false, rightClickPending_ = false, box_ = false;
    uint32_t pressedEntity_ = 0;              // the object under the left press (0: empty space)
    QPointF pressPos_, rightPressPos_, boxEnd_;
    rf::Vector2 virtualMouse_, lastMouse_;    // Shift slows a handle drag to a tenth of the mouse
    CameraTool camera_;
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
    CameraTool camera_; // the other buttons still move the camera during a stroke
};
