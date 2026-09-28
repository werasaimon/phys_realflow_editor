// The viewport's lights and cameras (see Viewport.h): the scene's lights into the mesh shader, the
// wireframes of lights and cameras, and the view through a scene camera. The shadows the lights cast
// are drawn in ViewportShadows.cpp.
#include "Viewport.h"

#include <algorithm>
#include <cmath>

using namespace rf;

namespace {

QVector3D qv(const Vector3& v) { return QVector3D(v.x, v.y, v.z); }
Vector3 rv(const QVector3D& v) { return Vector3(v.x(), v.y(), v.z()); }

} // namespace

// ---------------------------------------------------------------------------
// The scene's lights in the mesh shader
// ---------------------------------------------------------------------------
// Up to eight lights in the snapshot's order, in view space (the shader lights in view space):
//   - a sun as a direction (uLightSun 1: parallel rays, no fading, no cone);
//   - a lamp and a spotlight at their positions, fading to nothing at their range; a lamp's cone
//     cosines -2 and -1 make its cone factor 1 everywhere;
//   - for each, the shadow map it casts with this frame (uLightShadow: a slot of shadowMaps_, -1 none).
// The uniforms stay with the program for the whole frame.
void Viewport::applySceneLights(const QMatrix4x4& view) {
    QVector3D pos[8], dir[8], colour[8];
    float range[8] = {}, cosOuter[8] = {}, cosInner[8] = {};
    GLint sun[8] = {}, shadow[8] = {};
    auto slotOf = [this](int light) {
        for (int s = 0; s < kMaxShadowLights; ++s)
            if (shadowMaps_[s].light == light) return s;
        return -1;
    };
    int count = 0;
    for (size_t k = 0; k < snap_->lights.size() && count < 8; ++k) {
        const auto& l = snap_->lights[k];
        const bool spot = l.kind == int(LightKind::Spot);
        const float outer = 0.5f * l.coneDeg * kPi / 180.0f, inner = std::max(0.0f, 0.5f * l.coneDeg - l.softnessDeg) * kPi / 180.0f;
        pos[count] = view.map(qv(l.position));
        dir[count] = view.mapVector(qv(l.direction)).normalized();
        colour[count] = qv(l.color) * l.intensity;
        range[count] = std::max(l.range, 0.01f);
        cosOuter[count] = spot ? std::cos(outer) : -2.0f;
        cosInner[count] = spot ? std::max(std::cos(inner), cosOuter[count] + 1e-3f) : -1.0f;
        sun[count] = l.kind == int(LightKind::Sun) ? 1 : 0;
        shadow[count] = slotOf(int(k));
        ++count;
    }
    meshProg_.bind();
    meshProg_.setUniformValue("uLightCount", count);
    meshProg_.setUniformValueArray("uLightPos", pos, 8);
    meshProg_.setUniformValueArray("uLightDir", dir, 8);
    meshProg_.setUniformValueArray("uLightColor", colour, 8);
    meshProg_.setUniformValueArray("uLightRange", range, 8, 1);
    meshProg_.setUniformValueArray("uLightCosOuter", cosOuter, 8, 1);
    meshProg_.setUniformValueArray("uLightCosInner", cosInner, 8, 1);
    meshProg_.setUniformValueArray("uLightSun", sun, 8);
    meshProg_.setUniformValueArray("uLightShadow", shadow, 8);
    bindShadowMaps(view);
}

// ---------------------------------------------------------------------------
// Lights and cameras as wireframes
// ---------------------------------------------------------------------------
// A light in its own colour warmed a little, a camera pale blue-grey, the selected ones orange; the
// parts behind the geometry faint, so a lamp inside a box still shows.
void Viewport::drawSceneMarkers(const QMatrix4x4& vp) {
    if (markers_.empty()) return;
    const float aspect = float(width()) / float(std::max(1, height()));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const SceneMarker& m : markers_) {
        markerSolid_.clear();
        markerFaint_.clear();
        markerLines(m, aspect, markerSolid_, markerFaint_);
        // Below 1.0: the software renderer (llvmpipe) blends a channel of exactly 1.0 at alpha < 1 to 0.
        const Vector3 warm = vmin(m.color * 0.5f + Vector3(1.0f, 0.86f, 0.45f) * 0.5f, Vector3(0.98f));
        QVector4D c = m.kind == SceneMarker::Camera ? QVector4D(0.80f, 0.86f, 0.95f, 0.95f) : QVector4D(warm.x, warm.y, warm.z, 0.95f);
        if (m.selected) c = QVector4D(0.98f, 0.62f, 0.2f, 1.0f);
        QVector4D hidden = c, faint = c;
        hidden.setW(0.3f);
        faint.setW(0.35f);
        glDepthFunc(GL_GREATER);
        drawLines(markerSolid_, GL_LINES, vp, hidden, 1);
        glDepthFunc(GL_LEQUAL);
        drawLines(markerSolid_, GL_LINES, vp, c, 1);
        drawLines(markerFaint_, GL_LINES, vp, faint, 1);
    }
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------
// Through a camera
// ---------------------------------------------------------------------------
// A frame that matches the view within float noise (the camera written back from the view and sent
// again) leaves the view alone: only a real change of the camera moves it.
void Viewport::setLookThrough(bool on, const Vector3& eye, const Vector3& forward, const Vector3& up, float fovDeg, float nearClip,
                              float farClip) {
    if (!on) {
        if (!lookThrough_) return;
        lookThrough_ = false;
        camera_ = editorCamera_;
        update();
        updateHint();
        return;
    }
    const bool entering = !lookThrough_;
    if (entering) editorCamera_ = camera_;
    lookThrough_ = true;
    camera_.setLens(fovDeg, nearClip, farClip);
    const bool same = (qv(eye) - lookEye_).length() < 1e-4f && (qv(forward) - lookForward_).length() < 1e-4f &&
                      (qv(up) - lookUp_).length() < 1e-4f;
    if (entering || !same) {
        camera_.setEyeFrame(qv(eye), qv(forward).normalized(), qv(up).normalized());
        lookEye_ = camera_.eye();
        lookForward_ = camera_.forward();
        lookUp_ = camera_.up();
    }
    update();
}

// Called before every frame: a view that moved since it was last set or reported moves the camera.
void Viewport::syncLookThrough() {
    if (!lookThrough_) return;
    const QVector3D e = camera_.eye(), f = camera_.forward(), u = camera_.up();
    if ((e - lookEye_).length() < 1e-6f && (f - lookForward_).length() < 1e-7f && (u - lookUp_).length() < 1e-7f) return;
    lookEye_ = e;
    lookForward_ = f;
    lookUp_ = u;
    emit lookThroughMoved(rv(e), rv(f), rv(u));
}
