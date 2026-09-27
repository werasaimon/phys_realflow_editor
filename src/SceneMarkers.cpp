// The wireframes of lights and cameras, and picking them (see SceneMarkers.h).
#include "SceneMarkers.h"

#include <algorithm>
#include <cmath>

using namespace rf;

namespace {

const float kDegToRad = 3.14159265f / 180.0f;
const float kCameraDepth = 0.35f; // how far in front of a camera's eye its frame is drawn, m

void segment(std::vector<float>& out, const Vector3& a, const Vector3& b) { out.insert(out.end(), {a.x, a.y, a.z, 0, b.x, b.y, b.z, 0}); }

// A circle around `centre` in the plane of the unit vectors u and v.
void circle(std::vector<float>& out, const Vector3& centre, const Vector3& u, const Vector3& v, float radius, int segments = 32) {
    for (int k = 0; k < segments; ++k) {
        const float a0 = 6.2831853f * float(k) / float(segments), a1 = 6.2831853f * float(k + 1) / float(segments);
        segment(out, centre + (u * std::cos(a0) + v * std::sin(a0)) * radius, centre + (u * std::cos(a1) + v * std::sin(a1)) * radius);
    }
}

void sunLines(const SceneMarker& m, std::vector<float>& solid) {
    const Vector3 d = m.rotation.rotate(Vector3(0, -1, 0)), u = m.rotation.rotate(Vector3(1, 0, 0)), v = m.rotation.rotate(Vector3(0, 0, 1));
    circle(solid, m.position, u, v, 0.15f);
    for (int k = 0; k < 8; ++k) { // short rays around the disc
        const float a = 6.2831853f * float(k) / 8.0f;
        const Vector3 r = u * std::cos(a) + v * std::sin(a);
        segment(solid, m.position + r * 0.19f, m.position + r * 0.26f);
    }
    const Vector3 tip = m.position + d * 0.7f;
    segment(solid, m.position, tip);
    segment(solid, tip, tip - d * 0.12f + u * 0.06f);
    segment(solid, tip, tip - d * 0.12f - u * 0.06f);
}

void pointLines(const SceneMarker& m, std::vector<float>& solid, std::vector<float>& faint) {
    const Vector3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
    circle(solid, m.position, x, z, 0.1f, 24);
    circle(solid, m.position, x, y, 0.1f, 24);
    circle(solid, m.position, y, z, 0.1f, 24);
    segment(solid, m.position - y * 0.1f, m.position - y * 0.2f); // the base of the bulb
    if (m.selected) circle(faint, m.position, x, z, m.range, 64);
}

void spotLines(const SceneMarker& m, std::vector<float>& solid, std::vector<float>& faint) {
    const Vector3 d = m.rotation.rotate(Vector3(0, -1, 0)), u = m.rotation.rotate(Vector3(1, 0, 0)), v = m.rotation.rotate(Vector3(0, 0, 1));
    const float length = std::clamp(m.range, 0.3f, 1.0f);
    const float outer = 0.5f * m.coneDeg * kDegToRad, inner = std::max(0.0f, 0.5f * m.coneDeg - m.softnessDeg) * kDegToRad;
    const Vector3 end = m.position + d * length;
    const float r = length * std::tan(std::min(outer, 1.45f));
    circle(solid, end, u, v, r);
    for (int k = 0; k < 4; ++k) {
        const float a = 1.5707963f * float(k);
        segment(solid, m.position, end + (u * std::cos(a) + v * std::sin(a)) * r);
    }
    circle(faint, end, u, v, length * std::tan(std::min(inner, 1.45f)));
    circle(solid, m.position, u, v, 0.05f, 16);
}

void cameraLines(const SceneMarker& m, float aspect, std::vector<float>& solid) {
    const Vector3 f = m.rotation.rotate(Vector3(0, 0, -1)), up = m.rotation.rotate(Vector3(0, 1, 0)), right = m.rotation.rotate(Vector3(1, 0, 0));
    const float h = kCameraDepth * std::tan(0.5f * std::clamp(m.fovDeg, 1.0f, 170.0f) * kDegToRad), w = h * aspect;
    const Vector3 c = m.position + f * kCameraDepth;
    const Vector3 corners[4] = {c + right * w + up * h, c - right * w + up * h, c - right * w - up * h, c + right * w - up * h};
    for (int k = 0; k < 4; ++k) {
        segment(solid, m.position, corners[k]);
        segment(solid, corners[k], corners[(k + 1) % 4]);
    }
    const Vector3 top = c + up * (h * 1.1f);
    segment(solid, top - right * (w * 0.6f), top + right * (w * 0.6f));
    segment(solid, top - right * (w * 0.6f), top + up * (h * 0.6f));
    segment(solid, top + right * (w * 0.6f), top + up * (h * 0.6f));
}

} // namespace

void markerLines(const SceneMarker& m, float aspect, std::vector<float>& solid, std::vector<float>& faint) {
    switch (m.kind) {
    case SceneMarker::Sun: sunLines(m, solid); break;
    case SceneMarker::Point: pointLines(m, solid, faint); break;
    case SceneMarker::Spot: spotLines(m, solid, faint); break;
    case SceneMarker::Camera: cameraLines(m, aspect, solid); break;
    }
}

bool markerHit(const SceneMarker& m, const Ray& ray, float& t) {
    Vector3 centre = m.position;
    float radius = m.kind == SceneMarker::Sun ? 0.25f : 0.16f;
    if (m.kind == SceneMarker::Camera) centre = m.position + m.rotation.rotate(Vector3(0, 0, -1)) * (0.5f * kCameraDepth), radius = 0.24f;
    const Vector3 oc = ray.origin - centre;
    const float b = dot(oc, ray.dir), c = dot(oc, oc) - radius * radius, disc = b * b - c;
    if (disc < 0 || c <= 0) return false; // missed, or the eye is inside the ball (a camera made at the eye)
    t = -b - std::sqrt(disc);
    return t >= 0;
}
