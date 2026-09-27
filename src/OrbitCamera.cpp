// The camera's arithmetic (see OrbitCamera.h): the eye from yaw, pitch and distance around the
// target, the view and projection matrices, turning around any point so that it keeps its place on
// screen, panning, zooming towards a point, and the ray under a pixel.
#include "OrbitCamera.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

QVector3D OrbitCamera::eye() const {
    float cy = std::cos(qDegreesToRadians(yaw_)), sy = std::sin(qDegreesToRadians(yaw_));
    float cp = std::cos(qDegreesToRadians(pitch_)), sp = std::sin(qDegreesToRadians(pitch_));
    return target_ + distance_ * QVector3D(cp * cy, sp, cp * sy);
}

// The roll turns the picture about the view axis: the view is Rz(-roll) after the plain look-at,
// the inverse of a camera turned Ry Rx Rz (rf::objectRotation).
QMatrix4x4 OrbitCamera::view() const {
    QMatrix4x4 v;
    v.lookAt(eye(), target_, QVector3D(0, 1, 0));
    if (roll_ != 0.0f) {
        QMatrix4x4 r;
        r.rotate(-roll_, 0, 0, 1);
        v = r * v;
    }
    return v;
}

QMatrix4x4 OrbitCamera::projection(float aspect) const {
    QMatrix4x4 p;
    if (far_ > 0.0f) p.perspective(fov_, aspect, near_, far_);
    else p.perspective(fov_, aspect, std::max(0.005f, distance_ * 0.01f), distance_ * 50.0f + 50.0f);
    return p;
}

// yaw and pitch from the forward direction (as lookAlong), the target ahead of the eye, and the
// roll: the angle from the look-at's own up (world up kept upright) to the wanted up.
void OrbitCamera::setEyeFrame(const QVector3D& eyeAt, const QVector3D& forwardDir, const QVector3D& upDir) {
    distance_ = std::max(distance_, 1.0f);
    lookAlong(forwardDir);
    const float cy = std::cos(qDegreesToRadians(yaw_)), sy = std::sin(qDegreesToRadians(yaw_));
    const float cp = std::cos(qDegreesToRadians(pitch_)), sp = std::sin(qDegreesToRadians(pitch_));
    const QVector3D toEye(cp * cy, sp, cp * sy);
    target_ = eyeAt - distance_ * toEye;
    const QVector3D f = -toEye;
    const QVector3D right0 = QVector3D::crossProduct(f, QVector3D(0, 1, 0)).normalized();
    const QVector3D up0 = QVector3D::crossProduct(right0, f);
    roll_ = qRadiansToDegrees(std::atan2(-QVector3D::dotProduct(upDir, right0), QVector3D::dotProduct(upDir, up0)));
}

void OrbitCamera::setLens(float fovDeg, float nearClip, float farClip) {
    fov_ = std::clamp(fovDeg, 1.0f, 170.0f);
    near_ = std::max(nearClip, 1e-4f);
    far_ = std::max(farClip, near_ * 2.0f);
}

void OrbitCamera::rotate(float dx, float dy) {
    yaw_ += dx * 0.4f;
    pitch_ = std::clamp(pitch_ + dy * 0.4f, -89.0f, 89.0f);
}

// dir(yaw, pitch) points from the target to the eye. A yaw step turns every offset from the pivot
// about the world's up axis; a pitch step turns it about the horizontal axis at right angles to the
// view (Rodrigues' formula), so the eye, the target and the view direction turn as one.
void OrbitCamera::orbitAround(const QVector3D& pivot, float dx, float dy) {
    const float dyaw = dx * 0.4f;
    const float newPitch = std::clamp(pitch_ + dy * 0.4f, -89.0f, 89.0f), dpitch = newPitch - pitch_;
    QVector3D v = eye() - pivot;
    const float a = qDegreesToRadians(dyaw), ca = std::cos(a), sa = std::sin(a);
    v = QVector3D(v.x() * ca - v.z() * sa, v.y(), v.x() * sa + v.z() * ca);
    yaw_ += dyaw;
    const float yr = qDegreesToRadians(yaw_);
    const QVector3D axis(-std::sin(yr), 0.0f, std::cos(yr)); // horizontal, across the view
    const float b = qDegreesToRadians(dpitch), cb = std::cos(b), sb = std::sin(b);
    v = v * cb + QVector3D::crossProduct(axis, v) * sb + axis * QVector3D::dotProduct(axis, v) * (1.0f - cb);
    pitch_ = newPitch;
    const QVector3D eyeNow = pivot + v;
    target_ = eyeNow - (eye() - target_); // eye() - target_ is the new direction times the distance
}

QVector3D OrbitCamera::right() const {
    const QMatrix4x4 v = view();
    return QVector3D(v(0, 0), v(0, 1), v(0, 2));
}

QVector3D OrbitCamera::up() const {
    const QMatrix4x4 v = view();
    return QVector3D(v(1, 0), v(1, 1), v(1, 2));
}

void OrbitCamera::zoomToward(const QVector3D& point, float steps) {
    const float before = distance_;
    zoom(steps);
    const float k = distance_ / before; // the eye keeps its direction: the point stays under the cursor
    target_ = point + (target_ - point) * k;
}

void OrbitCamera::lookAlong(const QVector3D& forward) {
    const QVector3D d = -forward.normalized(); // from the target to the eye
    pitch_ = std::clamp(qRadiansToDegrees(std::asin(std::clamp(d.y(), -1.0f, 1.0f))), -89.0f, 89.0f);
    if (std::fabs(d.x()) + std::fabs(d.z()) > 1e-4f) yaw_ = qRadiansToDegrees(std::atan2(d.z(), d.x()));
}

void OrbitCamera::pan(float dx, float dy) {
    QMatrix4x4 v = view();
    QVector3D right(v(0, 0), v(0, 1), v(0, 2)), up(v(1, 0), v(1, 1), v(1, 2));
    float s = distance_ * 0.0015f;
    target_ += (-right * dx + up * dy) * s;
}

void OrbitCamera::zoom(float steps) {
    distance_ = std::clamp(distance_ * std::pow(0.88f, steps), 0.05f, 500.0f);
}

void OrbitCamera::frame(const rf::AABB& box, float yawDeg, float pitchDeg) {
    if (!box.valid()) return;
    rf::Vector3 c = box.center();
    target_ = QVector3D(c.x, c.y, c.z);
    float radius = 0.5f * rf::length(box.extent());
    distance_ = radius / std::sin(qDegreesToRadians(fov_ * 0.5f)) * 0.95f;
    yaw_ = yawDeg;
    pitch_ = pitchDeg;
}

Ray OrbitCamera::screenRay(const QPointF& px, const QSize& vp) const {
    const float w = std::max(1, vp.width()), h = std::max(1, vp.height());
    const float x = 2.0f * float(px.x()) / w - 1.0f;
    const float y = 1.0f - 2.0f * float(px.y()) / h;
    QMatrix4x4 inv = (projection(w / h) * view()).inverted();
    QVector3D nearP = inv.map(QVector3D(x, y, -1.0f)); // map() performs the perspective divide
    QVector3D farP = inv.map(QVector3D(x, y, 1.0f));
    QVector3D d = (farP - nearP).normalized();
    return Ray{rf::Vector3(nearP.x(), nearP.y(), nearP.z()), rf::Vector3(d.x(), d.y(), d.z())};
}

float intersectAxisPlane(const Ray& r, int axis, float coord) {
    if (std::fabs(r.dir[axis]) < 1e-8f) return -1.0f;
    float t = (coord - r.origin[axis]) / r.dir[axis];
    return t >= 0 ? t : -1.0f;
}

bool intersectBox(const Ray& r, const rf::AABB& b, float& tNear, float& tFar) {
    tNear = 0.0f;
    tFar = rf::kInf;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(r.dir[a]) < 1e-12f) {
            if (r.origin[a] < b.lo[a] || r.origin[a] > b.hi[a]) return false;
            continue;
        }
        float t0 = (b.lo[a] - r.origin[a]) / r.dir[a], t1 = (b.hi[a] - r.origin[a]) / r.dir[a];
        if (t0 > t1) std::swap(t0, t1);
        tNear = std::max(tNear, t0);
        tFar = std::min(tFar, t1);
        if (tNear > tFar) return false;
    }
    return true;
}
