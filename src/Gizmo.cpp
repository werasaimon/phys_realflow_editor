// The gizmo's geometry and arithmetic (see Gizmo.h): which handle is under the mouse, where a drag
// takes the object, and the triangles to draw. Handles are tested in screen space (pixels): an
// arrow by the distance from the mouse to its projected segment, a ring by the distance to its
// projected polyline, a square by whether the mouse is inside its projected corners. The squares
// and the centre win over the shafts where they overlap.
#include "Gizmo.h"

#include <QMatrix4x4>
#include <QVector4D>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace rf;

namespace {

// The colours of professional packages: X red, Y green, Z blue, the lit handle yellow.
const Vector3 kAxisColor[3] = {{0.878f, 0.278f, 0.298f}, {0.482f, 0.753f, 0.263f}, {0.239f, 0.561f, 0.878f}};
const Vector3 kHoverColor{1.0f, 0.824f, 0.290f};
const Vector3 kWhite{1.0f, 1.0f, 1.0f};
constexpr int kRingSegments = 96;

int axisOf(GizmoHandle h) { return int(h) - int(GizmoHandle::AxisX); }   // AxisX..AxisZ -> 0..2
int planeOf(GizmoHandle h) { return int(h) - int(GizmoHandle::PlaneYZ); } // the plane's normal axis
bool isAxis(GizmoHandle h) { return h == GizmoHandle::AxisX || h == GizmoHandle::AxisY || h == GizmoHandle::AxisZ; }
bool isPlane(GizmoHandle h) { return h == GizmoHandle::PlaneYZ || h == GizmoHandle::PlaneXZ || h == GizmoHandle::PlaneXY; }

float distanceToSegment(const Vector2& p, const Vector2& a, const Vector2& b) {
    const Vector2 ab = b - a;
    const float l2 = dot(ab, ab);
    const float t = l2 > 0 ? clampv(dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
    return length(p - (a + ab * t));
}

// Inside a convex quadrilateral on screen (either winding).
bool insideQuad(const Vector2& p, const Vector2 q[4]) {
    int positive = 0, negative = 0;
    for (int i = 0; i < 4; ++i) {
        const Vector2 e = q[(i + 1) % 4] - q[i], d = p - q[i];
        const float c = e.x * d.y - e.y * d.x;
        positive += c > 0;
        negative += c < 0;
    }
    return positive == 0 || negative == 0;
}

// The angle of a pixel around a centre, anticlockwise as the eye sees it (screen y points down).
float screenAngle(const Vector2& p, const Vector2& c) { return std::atan2(-(p.y - c.y), p.x - c.x); }

float wrapPi(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

void pushVertex(std::vector<float>& d, const Vector3& p) { d.insert(d.end(), {p.x, p.y, p.z, 0.0f}); }

void pushTriangle(std::vector<float>& d, const Vector3& a, const Vector3& b, const Vector3& c) {
    pushVertex(d, a);
    pushVertex(d, b);
    pushVertex(d, c);
}

void pushQuad(std::vector<float>& d, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& e) {
    pushTriangle(d, a, b, c);
    pushTriangle(d, a, c, e);
}

Vector3 brighter(const Vector3& c, float f) { return vmin(c * f, Vector3(1.0f)); }

} // namespace

void appendStroke(std::vector<float>& d, const Vector3& a, const Vector3& b, const Vector3& eye, float width) {
    Vector3 side = cross(b - a, eye - 0.5f * (a + b));
    const float l = length(side);
    if (l < 1e-12f) return;
    side *= 0.5f * width / l;
    pushQuad(d, a - side, b - side, b + side, a + side);
}

// ---------------------------------------------------------------------------
// The camera's view of the world
// ---------------------------------------------------------------------------
Vector2 GizmoView::project(const Vector3& p) const {
    const float w = float(std::max(1, size.width())), h = float(std::max(1, size.height()));
    const QMatrix4x4 vp = camera.projection(w / h) * camera.view();
    const QVector4D c = vp * QVector4D(p.x, p.y, p.z, 1.0f);
    if (c.w() <= 1e-6f) return Vector2(1e9f, 1e9f);
    return Vector2((c.x() / c.w() + 1.0f) * 0.5f * w, (1.0f - c.y() / c.w()) * 0.5f * h);
}

Ray GizmoView::ray(const Vector2& pixel) const { return camera.screenRay(QPointF(pixel.x, pixel.y), size); }

Vector3 GizmoView::eye() const {
    const QVector3D e = camera.eye();
    return Vector3(e.x(), e.y(), e.z());
}

Vector3 GizmoView::forward() const {
    const QVector3D f = camera.forward();
    return Vector3(f.x(), f.y(), f.z());
}

void GizmoView::screenAxes(Vector3& right, Vector3& up) const {
    const Vector3 f = forward();
    right = cross(f, Vector3(0, 1, 0));
    if (length2(right) < 1e-8f) right = Vector3(1, 0, 0);
    right = normalize(right);
    up = cross(right, f);
}

float GizmoView::worldPerPixel(const Vector3& at) const {
    const float depth = std::max(1e-4f, dot(at - eye(), forward()));
    return 2.0f * depth * std::tan(degToRad(camera.fovDeg() * 0.5f)) / float(std::max(1, size.height()));
}

// ---------------------------------------------------------------------------
// Frame and size
// ---------------------------------------------------------------------------
void Gizmo::setTarget(const Vector3& position, const Quaternion& rotation) {
    position_ = position;
    rotation_ = rotation;
}

// Scaling always works along the object's own axes (its size is measured along them).
Vector3 Gizmo::axis(int k) const {
    Vector3 e(0.0f);
    e[k] = 1.0f;
    return local_ || mode_ == GizmoMode::Scale ? normalize(rotation_.rotate(e)) : e;
}

float Gizmo::armLength(const GizmoView& v) const { return kArrowPixels * screenScale_ * v.worldPerPixel(position_); }

bool Gizmo::axisUsable(const GizmoView& v, int k) const {
    return length(v.project(position_ + axis(k) * armLength(v)) - v.project(position_)) > 16.0f;
}

Vector3 Gizmo::facingSign(const GizmoView& v) const {
    const Vector3 toEye = v.eye() - position_;
    return Vector3(dot(axis(0), toEye) >= 0 ? 1.0f : -1.0f, dot(axis(1), toEye) >= 0 ? 1.0f : -1.0f,
                   dot(axis(2), toEye) >= 0 ? 1.0f : -1.0f);
}

// The square across axis k sits in the quadrant of its plane that faces the camera (as in Blender).
void Gizmo::planeSquare(const GizmoView& v, int k, Vector3 c[4]) const {
    const float L = armLength(v), s0 = 0.22f * L, s1 = 0.42f * L;
    const Vector3 sign = facingSign(v);
    const Vector3 a = axis((k + 1) % 3) * sign[(k + 1) % 3], b = axis((k + 2) % 3) * sign[(k + 2) % 3];
    c[0] = position_ + a * s0 + b * s0;
    c[1] = position_ + a * s1 + b * s0;
    c[2] = position_ + a * s1 + b * s1;
    c[3] = position_ + a * s0 + b * s1;
}

// ---------------------------------------------------------------------------
// Which handle is under the mouse
// ---------------------------------------------------------------------------
GizmoHandle Gizmo::hitTest(const GizmoView& v, const Vector2& mouse) const {
    switch (mode_) {
    case GizmoMode::Translate: return hitTranslate(v, mouse);
    case GizmoMode::Rotate: return hitRotate(v, mouse);
    case GizmoMode::Scale: return hitScale(v, mouse);
    case GizmoMode::Select: break;
    }
    return GizmoHandle::None;
}

GizmoHandle Gizmo::nearestAxis(const GizmoView& v, const Vector2& mouse, float from, float to) const {
    const float L = armLength(v);
    float best = kPickPixels;
    GizmoHandle hit = GizmoHandle::None;
    for (int k = 0; k < 3; ++k) {
        if (!axisUsable(v, k)) continue;
        const float d = distanceToSegment(mouse, v.project(position_ + axis(k) * (L * from)), v.project(position_ + axis(k) * (L * to)));
        if (d < best) {
            best = d;
            hit = GizmoHandle(int(GizmoHandle::AxisX) + k);
        }
    }
    return hit;
}

GizmoHandle Gizmo::hitTranslate(const GizmoView& v, const Vector2& mouse) const {
    if (length(mouse - v.project(position_)) < 10.0f) return GizmoHandle::Center;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(dot(axis(k), v.forward())) < 0.15f) continue; // seen edge-on: not drawn
        Vector3 c[4];
        planeSquare(v, k, c);
        const Vector2 q[4] = {v.project(c[0]), v.project(c[1]), v.project(c[2]), v.project(c[3])};
        if (insideQuad(mouse, q)) return GizmoHandle(int(GizmoHandle::PlaneYZ) + k);
    }
    return nearestAxis(v, mouse, 0.14f, 1.0f);
}

GizmoHandle Gizmo::hitRotate(const GizmoView& v, const Vector2& mouse) const {
    const float R = ringRadius(v);
    float best = kPickPixels;
    GizmoHandle hit = GizmoHandle::None;
    for (int k = 0; k < 3; ++k) { // the ring around axis k lies in the plane of the other two
        const Vector3 a = axis((k + 1) % 3), b = axis((k + 2) % 3);
        Vector2 prev = v.project(position_ + a * R);
        for (int i = 1; i <= kRingSegments; ++i) {
            const float t = 2 * kPi * float(i) / kRingSegments;
            const Vector2 p = v.project(position_ + (a * std::cos(t) + b * std::sin(t)) * R);
            const float d = distanceToSegment(mouse, prev, p);
            if (d < best) {
                best = d;
                hit = GizmoHandle(int(GizmoHandle::AxisX) + k);
            }
            prev = p;
        }
    }
    const float viewRing = viewRingRadius(v) / v.worldPerPixel(position_); // a circle on screen
    if (std::fabs(length(mouse - v.project(position_)) - viewRing) < best) hit = GizmoHandle::View;
    return hit;
}

GizmoHandle Gizmo::hitScale(const GizmoView& v, const Vector2& mouse) const {
    const Vector2 d = mouse - v.project(position_);
    if (std::fabs(d.x) < 10.0f && std::fabs(d.y) < 10.0f) return GizmoHandle::Center;
    return nearestAxis(v, mouse, 0.14f, 1.07f);
}

// ---------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------
// The plane of a ring: its axis, and u, w in the plane with w = axis x u (a turn by +a about the
// axis takes the angle phi to phi + a). During a drag the frame fixed at the press is used.
void Gizmo::ringBasis(GizmoHandle h, const GizmoView& v, Vector3& ax, Vector3& u, Vector3& w) const {
    (void)v;
    if (isAxis(h)) {
        const int k = axisOf(h);
        ax = axes_[k];
        u = axes_[(k + 1) % 3];
        w = axes_[(k + 2) % 3];
        return;
    }
    ax = viewAxis_;
    u = cross(Vector3(0, 1, 0), ax);
    u = length2(u) < 1e-8f ? Vector3(1, 0, 0) : normalize(u);
    w = cross(ax, u);
}

void Gizmo::begin(GizmoHandle h, const GizmoView& v, const Vector2& mouse) {
    active_ = h;
    dragMode_ = mode_;
    startPosition_ = planePoint_ = position_;
    startRotation_ = rotation_;
    for (int k = 0; k < 3; ++k) axes_[k] = axis(k);
    startMouse_ = mouse;
    centreOnScreen_ = v.project(position_);
    lastScreenAngle_ = screenAngle(mouse, centreOnScreen_);
    turned_ = angleDeg_ = 0;
    last_ = GizmoPose{dragMode_, position_, rotation_, Vector3(1.0f)};
    viewAxis_ = normalize(v.eye() - position_);
    const Ray r = v.ray(mouse);
    if (dragMode_ == GizmoMode::Rotate) { // where the pie slice starts: the pressed point in the ring's plane
        Vector3 ax, u, w, hit;
        ringBasis(h, v, ax, u, w);
        pieStart_ = rayPlane(r, position_, ax, hit) ? std::atan2(dot(hit - position_, w), dot(hit - position_, u)) : 0.0f;
    } else if (isAxis(h)) {
        if (!closestOnAxis(r, position_, axes_[axisOf(h)], startParam_)) startParam_ = 0;
        if (dragMode_ == GizmoMode::Scale && std::fabs(startParam_) < 1e-5f) startParam_ = armLength(v);
    } else if (dragMode_ == GizmoMode::Translate) {
        planeNormal_ = isPlane(h) ? axes_[planeOf(h)] : v.forward(); // the centre: the plane facing the eye
        if (!rayPlane(r, planePoint_, planeNormal_, startHit_)) startHit_ = position_;
    }
}

void Gizmo::switchHandle(GizmoHandle h, const GizmoView& v) {
    if (!dragging() || modal_ || h == active_ || h == GizmoHandle::None) return;
    if (isPlane(h) && dragMode_ != GizmoMode::Translate) return; // a plane only moves
    const GizmoMode was = mode_;
    mode_ = dragMode_;
    position_ = startPosition_;
    rotation_ = startRotation_;
    begin(h, v, startMouse_);
    mode_ = was;
}

void Gizmo::beginFloorDrag(const GizmoView& v, const Vector2& mouse, const Vector3& hit) {
    const GizmoMode was = mode_;
    mode_ = GizmoMode::Translate;
    begin(GizmoHandle::PlaneXZ, v, mouse);
    mode_ = was;
    for (int k = 0; k < 3; ++k) axes_[k] = Vector3(k == 0, k == 1, k == 2);
    planePoint_ = startHit_ = hit; // the grabbed point stays under the cursor
    planeNormal_ = Vector3(0, 1, 0);
}

GizmoPose Gizmo::drag(const GizmoView& v, const Vector2& mouse, bool snap) {
    switch (dragMode_) {
    case GizmoMode::Translate: dragTranslate(v, mouse, snap); break;
    case GizmoMode::Rotate: dragRotate(v, mouse, snap); break;
    case GizmoMode::Scale: dragScale(v, mouse, snap); break;
    case GizmoMode::Select: break;
    }
    applyTyped();
    return last_;
}

void Gizmo::end() {
    active_ = GizmoHandle::None;
    if (modal_) { // a keyboard transform borrowed the mode and the Local / World switch
        mode_ = modeBefore_;
        local_ = localBefore_;
    }
    modal_ = typed_ = false;
    modalAxis_ = -1;
}

GizmoPose Gizmo::dragTranslate(const GizmoView& v, const Vector2& mouse, bool snap) {
    const Ray r = v.ray(mouse);
    Vector3 delta(0.0f);
    if (isAxis(active_)) {
        float s;
        if (!closestOnAxis(r, startPosition_, axes_[axisOf(active_)], s)) return last_;
        const float d = snap ? snapTo(s - startParam_, 0.1f) : s - startParam_;
        delta = axes_[axisOf(active_)] * d;
    } else {
        Vector3 hit;
        if (!rayPlane(r, planePoint_, planeNormal_, hit)) return last_;
        const Vector3 moved = hit - startHit_;
        if (isPlane(active_)) { // only along the square's two axes
            const int k = planeOf(active_);
            const Vector3 a = axes_[(k + 1) % 3], b = axes_[(k + 2) % 3];
            float da = dot(moved, a), db = dot(moved, b);
            if (snap) da = snapTo(da, 0.1f), db = snapTo(db, 0.1f);
            delta = a * da + b * db;
        } else {
            delta = moved;
            if (snap)
                for (int k = 0; k < 3; ++k) delta[k] = snapTo(delta[k], 0.1f);
        }
    }
    last_.position = startPosition_ + delta;
    return last_;
}

// The turn follows the mouse around the object's centre on screen; the angle accumulates, so more
// than half a turn works. Anticlockwise on screen is positive when the axis points at the eye.
GizmoPose Gizmo::dragRotate(const GizmoView& v, const Vector2& mouse, bool snap) {
    const float a = screenAngle(mouse, centreOnScreen_);
    turned_ += wrapPi(a - lastScreenAngle_);
    lastScreenAngle_ = a;
    Vector3 ax, u, w;
    ringBasis(active_, v, ax, u, w);
    const float sign = dot(ax, v.eye() - startPosition_) >= 0 ? 1.0f : -1.0f;
    float deg = radToDeg(turned_) * sign;
    if (snap) deg = snapTo(deg, 15.0f);
    angleDeg_ = deg;
    last_.rotation = (Quaternion::fromAxisAngle(ax, degToRad(deg)) * startRotation_).normalized();
    return last_;
}

GizmoPose Gizmo::dragScale(const GizmoView& v, const Vector2& mouse, bool snap) {
    float f = 1.0f;
    if (modal_) { // keyboard scale: the mouse's distance from the centre, now / at the start
        f = length(mouse - centreOnScreen_) / std::max(1.0f, length(startMouse_ - centreOnScreen_));
    } else if (active_ == GizmoHandle::Center) { // even scale: right or up grows, left or down shrinks
        const Vector2 d = mouse - startMouse_;
        f = std::exp((d.x - d.y) / 150.0f);
    } else {
        float s;
        if (!closestOnAxis(v.ray(mouse), startPosition_, axes_[axisOf(active_)], s)) return last_;
        f = s / startParam_;
    }
    if (snap) f = snapTo(f, 0.1f);
    f = std::max(f, 0.01f);
    last_.scale = isAxis(active_) ? Vector3(1.0f) : Vector3(f);
    if (isAxis(active_)) last_.scale[axisOf(active_)] = f;
    return last_;
}

// ---------------------------------------------------------------------------
// Keyboard transforms
// ---------------------------------------------------------------------------
void Gizmo::beginModal(GizmoMode m, const GizmoView& v, const Vector2& mouse) {
    modeBefore_ = mode_;
    localBefore_ = local_;
    mode_ = modalMode_ = m;
    modalMouse_ = mouse;
    modalPosition_ = position_;
    modalRotation_ = rotation_;
    modalAxis_ = -1;
    modalPresses_ = 0;
    typed_ = false;
    begin(m == GizmoMode::Rotate ? GizmoHandle::View : GizmoHandle::Center, v, mouse);
    modal_ = true;
}

// The same key again goes world axis -> own axis -> free. The drag restarts from the transform's
// start with the new limit, so the object jumps to where the mouse puts it under that limit.
void Gizmo::constrainModal(int k, const GizmoView& v) {
    if (!modal_) return;
    if (k == modalAxis_) ++modalPresses_;
    else modalAxis_ = k, modalPresses_ = 1;
    if (modalPresses_ > 2) modalAxis_ = -1, modalPresses_ = 0;
    local_ = modalPresses_ == 2;
    position_ = modalPosition_;
    rotation_ = modalRotation_;
    const GizmoHandle free = modalMode_ == GizmoMode::Rotate ? GizmoHandle::View : GizmoHandle::Center;
    const bool keepTyped = typed_;
    begin(modalAxis_ < 0 ? free : GizmoHandle(int(GizmoHandle::AxisX) + modalAxis_), v, modalMouse_);
    modal_ = true;
    typed_ = keepTyped;
}

// A typed number replaces what the mouse says: metres along the axis (X when free), degrees about
// the axis (the view direction when free), or a scale factor.
void Gizmo::applyTyped() {
    if (!typed_) return;
    const bool axisHeld = isAxis(active_);
    const Vector3 ax = axisHeld ? axes_[axisOf(active_)] : Vector3(1, 0, 0);
    switch (dragMode_) {
    case GizmoMode::Translate: last_.position = startPosition_ + ax * typedValue_; break;
    case GizmoMode::Rotate:
        angleDeg_ = typedValue_;
        last_.rotation = (Quaternion::fromAxisAngle(axisHeld ? ax : viewAxis_, degToRad(typedValue_)) * startRotation_).normalized();
        break;
    case GizmoMode::Scale:
        last_.scale = axisHeld ? Vector3(1.0f) : Vector3(std::max(typedValue_, 0.01f));
        if (axisHeld) last_.scale[axisOf(active_)] = std::max(typedValue_, 0.01f);
        break;
    case GizmoMode::Select: break;
    }
}

std::string Gizmo::dragLabel() const {
    if (!dragging()) return {};
    static const char* names[3] = {"X", "Y", "Z"};
    const bool axisHeld = isAxis(active_);
    const int k = axisHeld ? axisOf(active_) : 0;
    char buf[128] = "";
    if (dragMode_ == GizmoMode::Translate) {
        const Vector3 d = last_.position - startPosition_;
        if (axisHeld) std::snprintf(buf, sizeof(buf), "%s %+.3f м", names[k], double(dot(d, axes_[k])));
        else std::snprintf(buf, sizeof(buf), "%.3f м  (x %+.3f  y %+.3f  z %+.3f)", double(length(d)), double(d.x), double(d.y), double(d.z));
    } else if (dragMode_ == GizmoMode::Rotate) {
        std::snprintf(buf, sizeof(buf), "%s %+.1f°", axisHeld ? names[k] : "вокруг взгляда", double(angleDeg_));
    } else if (dragMode_ == GizmoMode::Scale) {
        if (axisHeld) std::snprintf(buf, sizeof(buf), "%s ×%.2f", names[k], double(last_.scale[k]));
        else std::snprintf(buf, sizeof(buf), "×%.2f", double(last_.scale.x));
    }
    std::string s = buf;
    if (modal_ && modalAxis_ >= 0) s += local_ ? "  · своя ось" : "  · ось мира";
    return s;
}

// ---------------------------------------------------------------------------
// The picture
// ---------------------------------------------------------------------------
// Fills the pieces, reusing the vectors of `out`. A stroke is two pieces: a dark underlay 2 px
// wider at 40 % and the colour over it. A solid (cone, cube) is three pieces: its faces towards the
// light brighter, those away darker, so it reads as a small 3D object.
class Gizmo::Writer {
public:
    Writer(const GizmoView& view, std::vector<GizmoPiece>& out, const Vector3& pivot) : v(view), out_(out) {
        eye = v.eye();
        wpp = v.worldPerPixel(pivot);
        v.screenAxes(right, up);
        light = normalize(up * 0.7f - right * 0.35f + normalize(eye - pivot) * 0.6f);
    }
    int piece(const Vector3& color, float alpha) {
        if (used_ == out_.size()) out_.emplace_back();
        GizmoPiece& p = out_[used_];
        p.triangles.clear(); // keeps its memory: no allocation once the gizmo has been drawn
        p.color = color;
        p.alpha = alpha;
        return int(used_++);
    }
    struct Stroke { int under, over; float width; };
    Stroke stroke(const Vector3& color, float alpha, float pixels) {
        const int under = piece(Vector3(0.0f), 0.4f * alpha);
        return Stroke{under, piece(color, alpha), pixels * wpp};
    }
    void segment(const Stroke& s, const Vector3& a, const Vector3& b) {
        appendStroke(out_[size_t(s.under)].triangles, a, b, eye, s.width + 2.0f * wpp);
        appendStroke(out_[size_t(s.over)].triangles, a, b, eye, s.width);
    }
    struct Solid { int bright, mid, dark; Vector3 centre; };
    Solid solid(const Vector3& color, float alpha, const Vector3& centre) {
        return Solid{piece(brighter(color, 1.3f), alpha), piece(color, alpha), piece(color * 0.62f, alpha), centre};
    }
    void triangle(const Solid& s, const Vector3& a, const Vector3& b, const Vector3& c) {
        Vector3 n = cross(b - a, c - a);
        if (dot(n, (a + b + c) / 3.0f - s.centre) < 0) n = -n; // outward
        const float lightness = dot(normalize(n), light);
        const int p = lightness > 0.35f ? s.bright : lightness > -0.2f ? s.mid : s.dark;
        pushTriangle(out_[size_t(p)].triangles, a, b, c);
    }
    std::vector<float>& triangles(int p) { return out_[size_t(p)].triangles; }
    int count() const { return int(used_); }

    const GizmoView& v;
    Vector3 eye, right, up, light;
    float wpp = 0;

private:
    std::vector<GizmoPiece>& out_;
    size_t used_ = 0;
};

Vector3 Gizmo::handleColor(GizmoHandle h, const Vector3& base) const { return lit(h) ? kHoverColor : base; }

int Gizmo::pieces(const GizmoView& v, std::vector<GizmoPiece>& out) const {
    Writer w(v, out, position_);
    addGuide(w);
    switch (mode_) {
    case GizmoMode::Translate:
        addPlaneSquares(w);
        addArrows(w, false);
        addCentre(w, false);
        break;
    case GizmoMode::Rotate:
        addPie(w);
        addRings(w);
        break;
    case GizmoMode::Scale:
        addArrows(w, true);
        addCentre(w, true);
        break;
    case GizmoMode::Select: break;
    }
    return w.count();
}

// A thin line along the whole axis the drag is held to, through the pivot, in the axis colour.
void Gizmo::addGuide(Writer& w) const {
    if (!dragging() || !isAxis(active_)) return;
    const int k = axisOf(active_);
    const float L = armLength(w.v);
    const int p = w.piece(kAxisColor[k], 0.6f);
    appendStroke(w.triangles(p), position_ - axes_[k] * (60 * L), position_ + axes_[k] * (60 * L), w.eye, 1.2f * w.wpp);
}

// Arrows (translate): shafts with shaded cones; scale: shafts ending in shaded cubes.
void Gizmo::addArrows(Writer& w, bool cubes) const {
    const float L = armLength(w.v);
    const Vector3 frame[3] = {axis(0), axis(1), axis(2)};
    for (int k = 0; k < 3; ++k) {
        const GizmoHandle h = GizmoHandle(int(GizmoHandle::AxisX) + k);
        if (!axisUsable(w.v, k)) continue;
        const Vector3 color = handleColor(h, kAxisColor[k]), a = frame[k], tip = position_ + a * L;
        const Vector3 base = position_ + a * (L * (cubes ? 0.9f : 0.78f));
        w.segment(w.stroke(color, fade(h), lit(h) ? 3.5f : 2.5f), position_ + a * (L * 0.14f), base);
        if (cubes) {
            const float half = 0.06f * L;
            const Writer::Solid s = w.solid(color, fade(h), tip);
            for (int f = 0; f < 3; ++f) {
                const Vector3 n = frame[f] * half, e1 = frame[(f + 1) % 3] * half, e2 = frame[(f + 2) % 3] * half;
                for (float sgn : {-1.0f, 1.0f}) {
                    const Vector3 c = tip + n * sgn;
                    w.triangle(s, c - e1 - e2, c + e1 - e2, c + e1 + e2);
                    w.triangle(s, c - e1 - e2, c + e1 + e2, c - e1 + e2);
                }
            }
        } else {
            const Writer::Solid s = w.solid(color, fade(h), 0.5f * (base + tip));
            const Vector3 u = normalize(anyPerpendicular(a)), v2 = cross(a, u);
            const float r = 0.075f * L;
            for (int i = 0; i < 16; ++i) {
                const float t0 = 2 * kPi * i / 16, t1 = 2 * kPi * (i + 1) / 16;
                const Vector3 p0 = base + (u * std::cos(t0) + v2 * std::sin(t0)) * r, p1 = base + (u * std::cos(t1) + v2 * std::sin(t1)) * r;
                w.triangle(s, p0, p1, tip);
                w.triangle(s, p0, p1, base);
            }
        }
    }
}

// The plane squares: filled at 25 % (lit: 55 %) with a 1 px border, in the quadrant facing the eye.
void Gizmo::addPlaneSquares(Writer& w) const {
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(dot(axis(k), w.v.forward())) < 0.15f) continue; // seen edge-on: no handle
        const GizmoHandle h = GizmoHandle(int(GizmoHandle::PlaneYZ) + k);
        const Vector3 color = handleColor(h, kAxisColor[k]);
        Vector3 c[4];
        planeSquare(w.v, k, c);
        pushQuad(w.triangles(w.piece(color, (lit(h) ? 0.55f : 0.25f) * fade(h))), c[0], c[1], c[2], c[3]);
        const Writer::Stroke border = w.stroke(color, 0.9f * fade(h), 1.0f);
        for (int i = 0; i < 4; ++i) w.segment(border, c[i], c[(i + 1) % 4]);
    }
}

// Rings: dense polylines; the half turned away from the eye is dimmed to 30 %. The outer ring faces
// the camera and is white at 70 %.
void Gizmo::addRings(Writer& w) const {
    const float R = ringRadius(w.v);
    const Vector3 toEye = normalize(w.eye - position_);
    for (int k = 0; k < 3; ++k) {
        const GizmoHandle h = GizmoHandle(int(GizmoHandle::AxisX) + k);
        const Vector3 a = axis((k + 1) % 3), b = axis((k + 2) % 3), color = handleColor(h, kAxisColor[k]);
        const float width = lit(h) ? 3.5f : 2.5f;
        const Writer::Stroke front = w.stroke(color, fade(h), width);
        const Writer::Stroke back = w.stroke(color, (active_ == h ? 1.0f : 0.3f) * fade(h), width);
        for (int i = 0; i < kRingSegments; ++i) {
            const float t0 = 2 * kPi * i / kRingSegments, t1 = 2 * kPi * (i + 1) / kRingSegments;
            const Vector3 p0 = position_ + (a * std::cos(t0) + b * std::sin(t0)) * R;
            const Vector3 p1 = position_ + (a * std::cos(t1) + b * std::sin(t1)) * R;
            w.segment(dot(0.5f * (p0 + p1) - position_, toEye) < 0 ? back : front, p0, p1);
        }
    }
    const GizmoHandle view = GizmoHandle::View;
    const Writer::Stroke ring = w.stroke(handleColor(view, kWhite), (lit(view) ? 1.0f : 0.7f) * fade(view), lit(view) ? 3.5f : 2.5f);
    const float Rv = viewRingRadius(w.v);
    for (int i = 0; i < kRingSegments; ++i) {
        const float t0 = 2 * kPi * i / kRingSegments, t1 = 2 * kPi * (i + 1) / kRingSegments;
        w.segment(ring, position_ + (w.right * std::cos(t0) + w.up * std::sin(t0)) * Rv,
                  position_ + (w.right * std::cos(t1) + w.up * std::sin(t1)) * Rv);
    }
}

// While turning: a pie slice in the ring's plane from where the drag began to where it is now.
void Gizmo::addPie(Writer& w) const {
    if (!dragging() || dragMode_ != GizmoMode::Rotate) return;
    Vector3 ax, u, v2;
    ringBasis(active_, w.v, ax, u, v2);
    const float R = active_ == GizmoHandle::View ? viewRingRadius(w.v) : ringRadius(w.v);
    const float turn = degToRad(angleDeg_);
    const Vector3 color = isAxis(active_) ? kAxisColor[axisOf(active_)] : kWhite;
    const int fill = w.piece(color, 0.2f);
    const int steps = std::max(2, int(std::fabs(turn) / (2 * kPi) * kRingSegments) + 1);
    auto at = [&](float phi) { return position_ + (u * std::cos(phi) + v2 * std::sin(phi)) * R; };
    for (int i = 0; i < steps; ++i)
        pushTriangle(w.triangles(fill), position_, at(pieStart_ + turn * i / steps), at(pieStart_ + turn * (i + 1) / steps));
    const Writer::Stroke edge = w.stroke(color, 0.9f, 1.5f);
    w.segment(edge, position_, at(pieStart_));
    w.segment(edge, position_, at(pieStart_ + turn));
}

// The centre: a small white circle (move in the view plane) or a square (scale evenly).
void Gizmo::addCentre(Writer& w, bool square) const {
    const GizmoHandle h = GizmoHandle::Center;
    const Vector3 color = handleColor(h, kWhite);
    const float r = 6.0f * w.wpp;
    const Writer::Stroke s = w.stroke(color, 0.95f * fade(h), lit(h) ? 3.0f : 2.0f);
    if (square) {
        const Vector3 c[4] = {position_ + (-w.right - w.up) * r, position_ + (w.right - w.up) * r, position_ + (w.right + w.up) * r,
                              position_ + (-w.right + w.up) * r};
        pushQuad(w.triangles(w.piece(color, (lit(h) ? 0.6f : 0.3f) * fade(h))), c[0], c[1], c[2], c[3]);
        for (int i = 0; i < 4; ++i) w.segment(s, c[i], c[(i + 1) % 4]);
        return;
    }
    for (int i = 0; i < 24; ++i) {
        const float t0 = 2 * kPi * i / 24, t1 = 2 * kPi * (i + 1) / 24;
        w.segment(s, position_ + (w.right * std::cos(t0) + w.up * std::sin(t0)) * r, position_ + (w.right * std::cos(t1) + w.up * std::sin(t1)) * r);
    }
}

// ---------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------
Quaternion quaternionFromEulerDeg(const Vector3& d) {
    return Quaternion::fromAxisAngle({0, 1, 0}, degToRad(d.y)) * Quaternion::fromAxisAngle({1, 0, 0}, degToRad(d.x)) *
           Quaternion::fromAxisAngle({0, 0, 1}, degToRad(d.z));
}

// R = Ry(yaw) Rx(pitch) Rz(roll): the middle row gives the pitch (m[1][2] = -sin pitch), the third
// column the yaw, the second row the roll. With the pitch at +-90 degrees yaw and roll turn about
// the same axis: the roll is set to 0 and the whole turn goes into the yaw.
Vector3 eulerDegFromQuaternion(const Quaternion& q) {
    const Matrix3x3 R = q.normalized().toMatrix3x3();
    const float sinPitch = clampv(-R.m[1][2], -1.0f, 1.0f);
    const float pitch = std::asin(sinPitch);
    float yaw, roll;
    if (std::fabs(sinPitch) < 0.99999f) {
        yaw = std::atan2(R.m[0][2], R.m[2][2]);
        roll = std::atan2(R.m[1][0], R.m[1][1]);
    } else {
        yaw = std::atan2(-R.m[2][0], R.m[0][0]);
        roll = 0.0f;
    }
    return Vector3(radToDeg(pitch), radToDeg(yaw), radToDeg(roll));
}

float snapTo(float v, float step) { return std::round(v / step) * step; }

// Closest points of the line axisPoint + s a and the ray o + t d (a, d unit): with w = axisPoint - o
// and b = a.d, setting both derivatives of |w + s a - t d|^2 to zero gives s (1 - b^2) = b (d.w) - a.w.
bool closestOnAxis(const Ray& ray, const Vector3& axisPoint, const Vector3& axis, float& s) {
    const Vector3 w = axisPoint - ray.origin;
    const float b = dot(axis, ray.dir);
    const float denom = 1.0f - b * b;
    if (denom < 1e-6f) return false;
    s = (b * dot(ray.dir, w) - dot(axis, w)) / denom;
    return true;
}

bool rayPlane(const Ray& ray, const Vector3& point, const Vector3& normal, Vector3& hit) {
    const float denom = dot(ray.dir, normal);
    if (std::fabs(denom) < 1e-6f) return false;
    const float t = dot(point - ray.origin, normal) / denom;
    if (t <= 0) return false;
    hit = ray.at(t);
    return true;
}
