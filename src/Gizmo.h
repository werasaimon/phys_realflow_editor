#pragma once
// The gizmo: the handles on the selected object that move, turn and scale it with the mouse, as in
// Blender, Maya or 3ds Max. W shows three arrows (X red, Y green, Z blue) with three small squares for
// moving in a plane (in the quadrant facing the camera) and a white circle in the centre for moving in
// the view plane; E shows three rings (their far halves dimmed) and a white outer ring facing the
// camera; R shows three shafts ending in cubes and a centre square for scaling evenly. The handle
// under the mouse turns yellow; while one is dragged the others fade, a thin guide runs along its
// axis, a turn shows a pie slice from where it started, and the amount is written next to the cursor.
// The gizmo keeps its size on screen (110 px) however far the object is, and is drawn over everything.
//
// This file is only the geometry and the arithmetic - no widgets, no OpenGL - so the self-test can
// check it with numbers: which handle is under a pixel, where the object goes when a handle is
// dragged from one pixel to another, and the pieces to draw (triangles in world space, one colour
// each; strokes are thin quads facing the eye, 2.5 px wide, 3.5 px when lit, over a darker 40 %
// underlay so they read on any background).
//
// The drag arithmetic:
//   arrow   - the point of the axis closest to the mouse ray; the object moves by how far that
//             point slid along the axis since the press (so the grabbed point stays under the cursor);
//   square  - the mouse ray meets the handle's plane; the object moves by the change of that point;
//   ring    - the angle of the mouse around the object's centre on screen, counted from the press
//             (anticlockwise on screen is a positive turn when the axis points at the viewer);
//   scale   - the ratio of the axis point's distance from the centre now and at the press; the
//             centre square scales evenly as the mouse goes right / up.
// Ctrl snaps: 0.1 m, 15 degrees, 10 %. (Shift for fine moves is applied by the viewport to the mouse.)
//
// Keyboard transforms, as in Blender: G (move), Shift+R (turn), Shift+S (scale) start a transform
// that follows the mouse without grabbing a handle; X / Y / Z then limit it to an axis (the world
// axis, pressed again the object's own axis, a third time free again); typing a number sets the
// exact amount (metres, degrees, a factor); Enter or a click confirms, Esc or the right button
// cancels. A free keyboard scale uses the mouse's distance from the centre on screen.
#include "OrbitCamera.h"
#include "math/Math.h"

#include <QSize>

#include <string>
#include <vector>

enum class GizmoMode { Select, Translate, Rotate, Scale };

// The handles. Rotate uses AxisX/Y/Z for its rings and View for the outer ring; Scale uses
// AxisX/Y/Z for its cubes and Center for the even scale; Translate uses all but View.
enum class GizmoHandle { None, AxisX, AxisY, AxisZ, PlaneYZ, PlaneXZ, PlaneXY, Center, View };

// What the camera shows: the eye, the pixels, the ray under a pixel and back.
struct GizmoView {
    OrbitCamera camera;
    QSize size;

    rf::Vector2 project(const rf::Vector3& p) const; // pixels, top-left origin; far away if behind the eye
    Ray ray(const rf::Vector2& pixel) const;
    rf::Vector3 eye() const;
    rf::Vector3 forward() const;
    void screenAxes(rf::Vector3& right, rf::Vector3& up) const; // the screen's right and up in the world
    float worldPerPixel(const rf::Vector3& at) const;           // metres one pixel covers at that depth
};

// Where a drag has taken the object: new position and rotation, and the scale factors along the
// object's own axes (1, 1, 1 when it is not being scaled).
struct GizmoPose {
    GizmoMode mode = GizmoMode::Translate; // which of the three the drag was
    rf::Vector3 position;
    rf::Quaternion rotation;
    rf::Vector3 scale{1.0f};
};

// A piece of the gizmo's picture: triangles (x, y, z, 0 per vertex) in one colour.
struct GizmoPiece {
    std::vector<float> triangles;
    rf::Vector3 color;
    float alpha = 1.0f;
};

class Gizmo {
public:
    static constexpr float kArrowPixels = 110.0f; // from the centre to an arrow's tip, on screen
    static constexpr float kPickPixels = 8.0f;    // how close the mouse must be to a handle

    void setMode(GizmoMode m) { mode_ = m; }
    GizmoMode mode() const { return mode_; }
    void setLocal(bool on) { local_ = on; } // turn with the object (else the world axes)
    // The size on screen as a factor of 110 px (the + / - keys), 0.5 .. 2.5.
    void setScreenScale(float s) { screenScale_ = s < 0.5f ? 0.5f : (s > 2.5f ? 2.5f : s); }
    float screenScale() const { return screenScale_; }
    bool local() const { return local_; }
    void setTarget(const rf::Vector3& position, const rf::Quaternion& rotation);
    const rf::Vector3& position() const { return position_; }

    GizmoHandle hitTest(const GizmoView& v, const rf::Vector2& mouse) const;
    void setHover(GizmoHandle h) { hover_ = h; }
    GizmoHandle hover() const { return hover_; }

    void begin(GizmoHandle h, const GizmoView& v, const rf::Vector2& mouse);
    // Shift+drag of an object: slide it on the horizontal plane through the point that was hit.
    void beginFloorDrag(const GizmoView& v, const rf::Vector2& mouse, const rf::Vector3& hit);
    GizmoPose drag(const GizmoView& v, const rf::Vector2& mouse, bool snap);
    void end();
    // During a handle drag: X / Y / Z switch it to that axis, Shift+X / Y / Z to the plane across it
    // (moving only), as if that handle had been grabbed at the press (Blender's axis locking).
    void switchHandle(GizmoHandle h, const GizmoView& v);
    bool dragging() const { return active_ != GizmoHandle::None; }
    GizmoHandle activeHandle() const { return active_; }
    float dragAngleDeg() const { return angleDeg_; } // the turn so far
    const GizmoPose& lastPose() const { return last_; }
    const rf::Vector3& dragStart() const { return startPosition_; }

    // Keyboard transforms (see the top of the file)
    void beginModal(GizmoMode m, const GizmoView& v, const rf::Vector2& mouse);
    void constrainModal(int axis, const GizmoView& v); // X / Y / Z pressed: world, local, free in turn
    bool modal() const { return modal_; }
    int modalAxis() const { return modalAxis_; }
    void setTyped(bool typed, float value) { typed_ = typed, typedValue_ = value; }
    // The live amount next to the cursor: "X +0.350 м", "Z +45.0°", "×1.20".
    std::string dragLabel() const;

    // The picture, written into `out` (its vectors are reused frame to frame: no allocations once
    // warm); returns how many pieces of `out` are used.
    int pieces(const GizmoView& v, std::vector<GizmoPiece>& out) const;

    rf::Vector3 axis(int k) const;                   // world direction of the gizmo's axis k
    float armLength(const GizmoView& v) const;       // world length of an arrow (constant on screen)
    float ringRadius(const GizmoView& v) const { return 0.88f * armLength(v); }
    float viewRingRadius(const GizmoView& v) const { return 1.05f * armLength(v); }

private:
    class Writer; // fills the pieces (Gizmo.cpp)

    bool axisUsable(const GizmoView& v, int k) const; // not pointing straight at the camera
    rf::Vector3 facingSign(const GizmoView& v) const; // per axis: +1 if it points towards the eye
    void planeSquare(const GizmoView& v, int k, rf::Vector3 corners[4]) const;
    GizmoHandle hitTranslate(const GizmoView& v, const rf::Vector2& mouse) const;
    GizmoHandle hitRotate(const GizmoView& v, const rf::Vector2& mouse) const;
    GizmoHandle hitScale(const GizmoView& v, const rf::Vector2& mouse) const;
    GizmoHandle nearestAxis(const GizmoView& v, const rf::Vector2& mouse, float from, float to) const;
    GizmoPose dragTranslate(const GizmoView& v, const rf::Vector2& mouse, bool snap);
    GizmoPose dragRotate(const GizmoView& v, const rf::Vector2& mouse, bool snap);
    GizmoPose dragScale(const GizmoView& v, const rf::Vector2& mouse, bool snap);
    void applyTyped();
    void ringBasis(GizmoHandle h, const GizmoView& v, rf::Vector3& axis, rf::Vector3& u, rf::Vector3& w) const;
    void addArrows(Writer& out, bool cubes) const;
    void addPlaneSquares(Writer& out) const;
    void addRings(Writer& out) const;
    void addPie(Writer& out) const;
    void addCentre(Writer& out, bool square) const;
    void addGuide(Writer& out) const;
    rf::Vector3 handleColor(GizmoHandle h, const rf::Vector3& base) const;
    bool lit(GizmoHandle h) const { return hover_ == h || active_ == h; }
    // While a handle is dragged the others fade to 35 %: the eye goes to the one in use.
    float fade(GizmoHandle h) const { return active_ == GizmoHandle::None || active_ == h ? 1.0f : 0.35f; }

    GizmoMode mode_ = GizmoMode::Translate;
    bool local_ = false;
    float screenScale_ = 1.0f;
    rf::Vector3 position_{0.0f};
    rf::Quaternion rotation_;
    GizmoHandle hover_ = GizmoHandle::None;

    // The drag, as it was at the press
    GizmoHandle active_ = GizmoHandle::None;
    GizmoMode dragMode_ = GizmoMode::Translate;
    rf::Vector3 startPosition_{0.0f};
    rf::Quaternion startRotation_;
    rf::Vector3 axes_[3];               // the frame of the drag, fixed at the press
    rf::Vector3 planePoint_{0.0f}, planeNormal_{0.0f};
    rf::Vector3 startHit_{0.0f};        // plane drags: where the ray met the plane at the press
    rf::Vector3 viewAxis_{0, 0, 1};     // towards the eye at the press: the axis of the outer ring
    float startParam_ = 0;              // arrow drags: the axis parameter under the mouse at the press
    float pieStart_ = 0;                // ring drags: the angle in the ring's plane where the drag began
    rf::Vector2 startMouse_, centreOnScreen_;
    float lastScreenAngle_ = 0, turned_ = 0, angleDeg_ = 0;
    GizmoPose last_;

    // Keyboard transform
    bool modal_ = false, typed_ = false;
    float typedValue_ = 0;
    int modalAxis_ = -1, modalPresses_ = 0; // presses of the same axis key: 1 world, 2 local, 3 free
    bool localBefore_ = false;              // the Local / World switch as it was before the transform
    GizmoMode modeBefore_ = GizmoMode::Translate;
    GizmoMode modalMode_ = GizmoMode::Translate;
    rf::Vector2 modalMouse_;
    rf::Vector3 modalPosition_{0.0f};
    rf::Quaternion modalRotation_;
};

// The rotation of an Entity is stored as Euler angles in degrees: x pitch, y yaw, z roll, applied as
// Ry * Rx * Rz (the SDK's entityRotation). These convert between that form and a quaternion; at
// pitch +-90 degrees (gimbal lock) the roll is folded into the yaw.
rf::Quaternion quaternionFromEulerDeg(const rf::Vector3& deg);
rf::Vector3 eulerDegFromQuaternion(const rf::Quaternion& q);

// Rounds v to the nearest multiple of step.
float snapTo(float v, float step);

// The parameter s of the point axisPoint + s * axis closest to the ray (axis is unit length);
// false when the ray runs along the axis.
bool closestOnAxis(const Ray& ray, const rf::Vector3& axisPoint, const rf::Vector3& axis, float& s);
// Where the ray meets the plane; false if it runs along it or the plane is behind the eye.
bool rayPlane(const Ray& ray, const rf::Vector3& point, const rf::Vector3& normal, rf::Vector3& hit);
// A stroke from a to b as a thin quad facing the eye, `width` metres wide (two triangles appended).
void appendStroke(std::vector<float>& triangles, const rf::Vector3& a, const rf::Vector3& b, const rf::Vector3& eye, float width);
