// The viewport's lights and cameras (see Viewport.h): the scene's lights into the mesh shader, the
// sun's shadow map, the wireframes of lights and cameras, and the view through a scene camera.
//
// Shadows: the first sun with "shadows" on renders the depth of every body from the sun into a
// 2048 x 2048 map (an orthographic box around the world box), and the mesh shader compares each
// point's depth there on 3 x 3 texels (PCF: a soft edge a texel wide). Cloth, soft bodies and
// particles are lit and receive the shadow but do not cast one yet (TODO: they are built per frame).
#include "Viewport.h"

#include <algorithm>
#include <cmath>

using namespace rf;

namespace {

const int kShadowSize = 2048;
const int kShadowUnit = 5; // a texture unit no other pass uses

QVector3D qv(const Vector3& v) { return QVector3D(v.x, v.y, v.z); }
Vector3 rv(const QVector3D& v) { return Vector3(v.x(), v.y(), v.z()); }

// A body's model matrix: its pose, and for a box its half extents (the unit cube spans [-1, 1]).
QMatrix4x4 bodyMatrix(const RenderSnapshot::Body& b, const Vector3& scale) {
    const Matrix3x3 R = b.rot.toMatrix3x3();
    return QMatrix4x4(R.m[0][0] * scale.x, R.m[0][1] * scale.y, R.m[0][2] * scale.z, b.pos.x, R.m[1][0] * scale.x, R.m[1][1] * scale.y,
                      R.m[1][2] * scale.z, b.pos.y, R.m[2][0] * scale.x, R.m[2][1] * scale.y, R.m[2][2] * scale.z, b.pos.z, 0, 0, 0, 1);
}

// The sun the shader uses: the first one with shadows, else the first one (null: none).
const RenderSnapshot::LightInfo* chosenSun(const RenderSnapshot& s) {
    const RenderSnapshot::LightInfo* first = nullptr;
    for (const auto& l : s.lights) {
        if (l.kind != int(LightKind::Sun)) continue;
        if (l.shadows) return &l;
        if (!first) first = &l;
    }
    return first;
}

} // namespace

// ---------------------------------------------------------------------------
// The scene's lights in the mesh shader
// ---------------------------------------------------------------------------
// Lamps and spotlights go in view space (the shader lights in view space); a lamp's cone cosines
// -2 and -1 make its cone factor 1 everywhere. The uniforms stay with the program for the frame.
void Viewport::applySceneLights(const QMatrix4x4& view) {
    QVector3D pos[8], dir[8], colour[8];
    float range[8] = {}, cosOuter[8] = {}, cosInner[8] = {};
    int count = 0;
    for (const auto& l : snap_->lights) {
        if (l.kind == int(LightKind::Sun) || count == 8) continue;
        pos[count] = view.map(qv(l.position));
        dir[count] = view.mapVector(qv(l.direction)).normalized();
        colour[count] = qv(l.color) * l.intensity;
        range[count] = std::max(l.range, 0.01f);
        const bool spot = l.kind == int(LightKind::Spot);
        const float outer = 0.5f * l.coneDeg * kPi / 180.0f, inner = std::max(0.0f, 0.5f * l.coneDeg - l.softnessDeg) * kPi / 180.0f;
        cosOuter[count] = spot ? std::cos(outer) : -2.0f;
        cosInner[count] = spot ? std::max(std::cos(inner), cosOuter[count] + 1e-3f) : -1.0f;
        ++count;
    }
    const RenderSnapshot::LightInfo* sun = chosenSun(*snap_);
    meshProg_.bind();
    meshProg_.setUniformValue("uLightCount", count);
    meshProg_.setUniformValueArray("uLightPos", pos, 8);
    meshProg_.setUniformValueArray("uLightDir", dir, 8);
    meshProg_.setUniformValueArray("uLightColor", colour, 8);
    meshProg_.setUniformValueArray("uLightRange", range, 8, 1);
    meshProg_.setUniformValueArray("uLightCosOuter", cosOuter, 8, 1);
    meshProg_.setUniformValueArray("uLightCosInner", cosInner, 8, 1);
    meshProg_.setUniformValue("uSunOn", sun ? 1 : 0);
    if (sun) {
        meshProg_.setUniformValue("uSunDir", -view.mapVector(qv(sun->direction)).normalized());
        meshProg_.setUniformValue("uSunColor", qv(sun->color) * sun->intensity);
    }
    meshProg_.setUniformValue("uShadowOn", shadowOn_ ? 1 : 0);
    meshProg_.setUniformValue("uShadowMap", kShadowUnit);
    if (!shadowOn_) return;
    QMatrix4x4 bias; // clip space [-1, 1] -> texture space [0, 1]
    bias.translate(0.5f, 0.5f, 0.5f);
    bias.scale(0.5f);
    meshProg_.setUniformValue("uShadowFromView", bias * shadowVP_ * view.inverted());
    meshProg_.setUniformValue("uShadowTexel", 1.0f / float(kShadowSize));
    glActiveTexture(GL_TEXTURE0 + kShadowUnit);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE0);
}

// ---------------------------------------------------------------------------
// The sun's shadow map
// ---------------------------------------------------------------------------
bool Viewport::ensureShadowTarget() {
    if (shadowFbo_) return true;
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    if (!ok) qWarning("The shadow map framebuffer is incomplete: no shadows");
    return ok;
}

// The depth of every rigid body (and while playing the scenery) as the sun sees it.
void Viewport::drawShadowCasters() {
    shadowProg_.bind();
    shadowProg_.setUniformValue("uLightVP", shadowVP_);
    auto draw = [this](const RenderSnapshot::Body& b) {
        if (b.shape == ShapeType::Box) {
            shadowProg_.setUniformValue("uModel", bodyMatrix(b, b.halfExtents));
            cube_.vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, cube_.count);
            cube_.vao.release();
            return;
        }
        const bool sphere = b.shape == ShapeType::Sphere;
        if (!sphere && !b.mesh) return;
        GpuMesh& g = hullMesh(sphere ? unitSphere_ : b.mesh);
        shadowProg_.setUniformValue("uModel", bodyMatrix(b, Vector3(sphere ? b.radius : 1.0f)));
        g.vao->bind();
        glDrawArrays(GL_TRIANGLES, 0, g.count);
        g.vao->release();
    };
    for (const auto& b : snap_->bodies) draw(b);
    if (!editMode_)
        for (const auto& b : scenery_) draw(b);
}

void Viewport::renderSunShadow() {
    shadowOn_ = false;
    const RenderSnapshot::LightInfo* sun = chosenSun(*snap_);
    const AABB& d = snap_->domain;
    if (!sun || !sun->shadows || !d.valid() || !ensureShadowTarget()) return;
    const QVector3D centre = qv(d.center()), dir = qv(sun->direction).normalized();
    const float r = 0.5f * length(d.extent()) + 0.5f;
    QMatrix4x4 view, proj;
    view.lookAt(centre - dir * (2.0f * r), centre, std::fabs(dir.y()) > 0.99f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0));
    proj.ortho(-r, r, -r, r, 0.01f, 4.0f * r);
    shadowVP_ = proj * view;
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f); // no shadow acne on the lit faces themselves
    drawShadowCasters();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    shadowOn_ = true;
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
