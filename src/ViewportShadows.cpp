// The shadows of the scene's lights (see Viewport.h). Every light with "Отбрасывает тени" ticked -
// up to kMaxShadowLights of them a frame - first draws what it sees into a shadow map:
//   - a sun: the depth of every body seen from far along its rays, through an orthographic box that
//     holds the whole world (Williams 1978, "Casting curved shadows on curved surfaces");
//   - a spotlight: the same from where it stands, through a lens as wide as its cone and soft edge;
//   - a lamp: it shines every way, so it keeps, on the six faces of a cube around it, the distance to
//     the nearest surface in every direction (omnidirectional shadow maps; Gerasimov, "GPU Gems",
//     ch. 12, 2004).
// The mesh shader then asks the map whether a point is the nearest thing the light sees (Shaders.h:
// litByMap, litByCube) and softens the edge with a few neighbouring samples (percentage-closer
// filtering; Reeves, Salesin & Cook 1987).
//
// Why a budget: a sun or a spotlight redraws every body once more, a lamp six times more, every
// frame. So lamps and spotlights start without shadows (as in Blender, Unity and Unreal), and past
// kMaxShadowLights the suns keep theirs first, then the lamps and spotlights that look brightest from
// the eye; the others are named in the status bar (the shadowsLeftOut signal).
// What casts: rigid bodies, the scenery while playing, cloth and soft-body surfaces. The liquid's
// particles are drawn as sphere sprites and do not cast a shadow (yet).
#include "Viewport.h"

#include <QVector2D>

#include <algorithm>
#include <cmath>

using namespace rf;
using LightInfo = RenderSnapshot::LightInfo;

namespace {

const int kDepthMapSize = 2048; // a sun's or a spotlight's map: 2048 x 2048 depths, 16 MB of video memory
const int kCubeFaceSize = 512;  // a lamp's cube: 6 faces of 512 x 512 distances, 6 MB
// Texture units no other pass uses: 5 .. 8 the depth maps, 9 .. 12 the cubes. Every sampler keeps a unit
// of its own even while unused - OpenGL refuses to draw when a 2D and a cube sampler share one.
const int kFirstMapUnit = 5;
const int kFirstCubeUnit = kFirstMapUnit + Viewport::kMaxShadowLights;

QVector3D qv(const Vector3& v) { return QVector3D(v.x, v.y, v.z); }

// A body's model matrix: its pose, and for a box its half extents (the unit cube spans [-1, 1]).
QMatrix4x4 bodyMatrix(const RenderSnapshot::Body& b, const Vector3& scale) {
    const Matrix3x3 R = b.rot.toMatrix3x3();
    return QMatrix4x4(R.m[0][0] * scale.x, R.m[0][1] * scale.y, R.m[0][2] * scale.z, b.pos.x, R.m[1][0] * scale.x, R.m[1][1] * scale.y,
                      R.m[1][2] * scale.z, b.pos.y, R.m[2][0] * scale.x, R.m[2][1] * scale.y, R.m[2][2] * scale.z, b.pos.z, 0, 0, 0, 1);
}

// How much a light's shadow matters, to choose among too many: a sun lights everything (always
// first); a lamp or a spotlight by how bright it looks from the eye - its intensity over its distance.
float shadowPriority(const LightInfo& l, const QVector3D& eye) {
    if (l.kind == int(LightKind::Sun)) return 1e9f + l.intensity;
    return l.intensity / (1.0f + (qv(l.position) - eye).length());
}

// The depth-test state of a shadow pass: every caster writes its depth, nothing is blended.
void depthPassState(QOpenGLExtraFunctions& gl) {
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthMask(GL_TRUE);
    gl.glDepthFunc(GL_LESS);
    gl.glDisable(GL_BLEND);
}

} // namespace

// ---------------------------------------------------------------------------
// Choosing, then drawing
// ---------------------------------------------------------------------------
// The lights that cast a shadow this frame (indices into the snapshot's lights), best first; `without`
// gets the ticked ones over the budget. Only the first eight lights are lit at all (the shader's
// arrays), so only they are candidates.
std::vector<int> Viewport::pickShadowLights(std::vector<int>& without) const {
    const QVector3D eye = camera_.eye();
    std::vector<int> wanted;
    const int lit = std::min(8, int(snap_->lights.size()));
    for (int k = 0; k < lit; ++k)
        if (snap_->lights[size_t(k)].shadows) wanted.push_back(k);
    std::stable_sort(wanted.begin(), wanted.end(), [&](int a, int b) {
        return shadowPriority(snap_->lights[size_t(a)], eye) > shadowPriority(snap_->lights[size_t(b)], eye);
    });
    const size_t kept = std::min(wanted.size(), size_t(kMaxShadowLights));
    without.assign(wanted.begin() + std::ptrdiff_t(kept), wanted.end());
    wanted.resize(kept);
    return wanted;
}

// One frame's shadows, before the picture:
//   1. choose the lights (the budget above);
//   2. draw each one's map into its slot - a lamp's cube, a sun's or a spotlight's depth map;
//   3. tell the window when the lights left without a shadow changed (it names them in the status bar).
void Viewport::renderShadows() {
    std::vector<int> without;
    std::vector<int> chosen = pickShadowLights(without);
    // In the order of the lights, not of their brightness: a slot keeps its kind (map or cube) while
    // the camera moves, and the mesh shader is not rebuilt every time two of them trade places.
    std::sort(chosen.begin(), chosen.end());
    sunShadowDrawn_ = false;
    for (int s = 0; s < kMaxShadowLights; ++s) {
        ShadowMap& m = shadowMaps_[s];
        m.light = -1;
        if (s >= int(chosen.size())) continue;
        const LightInfo& light = snap_->lights[size_t(chosen[size_t(s)])];
        const bool drawn = light.kind == int(LightKind::Point) ? renderDistanceCube(s, light) : renderDepthMap(s, light);
        if (!drawn) continue;
        m.light = chosen[size_t(s)];
        sunShadowDrawn_ = sunShadowDrawn_ || light.kind == int(LightKind::Sun);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    if (without != shadowLess_) {
        shadowLess_ = without;
        emit shadowsLeftOut(shadowLess_);
    }
}

// A sun's or a spotlight's depth map into slot `slot`:
//   1. the light's eye - a sun from far along its rays, looking at the world box's centre through an
//      orthographic box that holds the whole world; a spotlight from where it stands, through a lens
//      as wide as its cone plus its soft edge, as far as its light reaches;
//   2. every caster's depth, each face pushed back a little by its slope (glPolygonOffset), so that a
//      lit face does not shadow itself ("shadow acne").
bool Viewport::renderDepthMap(int slot, const LightInfo& light) {
    ShadowMap& m = shadowMaps_[slot];
    const bool sun = light.kind == int(LightKind::Sun);
    const AABB& world = snap_->domain;
    if ((sun && !world.valid()) || !ensureDepthMap(slot)) return false;
    const QVector3D dir = qv(light.direction).normalized();
    const QVector3D up = std::fabs(dir.y()) > 0.99f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
    QMatrix4x4 view, proj;
    if (sun) {
        const QVector3D centre = qv(world.center());
        const float r = 0.5f * length(world.extent()) + 0.5f;
        view.lookAt(centre - dir * (2.0f * r), centre, up);
        proj.ortho(-r, r, -r, r, 0.01f, 4.0f * r);
        m.nearClip = m.farClip = 0.0f; // a sun's depth is linear already
    } else {
        const QVector3D at = qv(light.position);
        m.nearClip = 0.05f;
        m.farClip = std::max(light.range, 0.2f);
        view.lookAt(at, at + dir, up);
        proj.perspective(std::min(light.coneDeg + light.softnessDeg, 170.0f), 1.0f, m.nearClip, m.farClip);
    }
    m.cube = false;
    m.viewProj = proj * view;
    glBindFramebuffer(GL_FRAMEBUFFER, m.fbo);
    glViewport(0, 0, kDepthMapSize, kDepthMapSize);
    depthPassState(*this);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    shadowProg_.bind();
    shadowProg_.setUniformValue("uLightVP", m.viewProj);
    drawShadowCasters(shadowProg_);
    glDisable(GL_POLYGON_OFFSET_FILL);
    return true;
}

// A lamp's cube of distances into slot `slot`: six square views from the lamp, 90 degrees wide, one
// along each axis, turned as OpenGL lays out the faces of a cube map (the +x face with its up at -y,
// and so on); every pixel keeps the distance to the nearest caster in its direction, over the lamp's
// reach (1.0: nothing there).
bool Viewport::renderDistanceCube(int slot, const LightInfo& light) {
    static const QVector3D kFaceDir[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const QVector3D kFaceUp[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    ShadowMap& m = shadowMaps_[slot];
    if (!ensureDistanceCube(slot)) return false;
    m.cube = true;
    m.lampAt = light.position;
    m.farClip = std::max(light.range, 0.2f);
    const QVector3D at = qv(light.position);
    QMatrix4x4 proj;
    proj.perspective(90.0f, 1.0f, 0.02f, m.farClip);
    distanceProg_.bind();
    distanceProg_.setUniformValue("uLamp", at);
    distanceProg_.setUniformValue("uFar", m.farClip);
    glBindFramebuffer(GL_FRAMEBUFFER, m.cubeFbo);
    glViewport(0, 0, kCubeFaceSize, kCubeFaceSize);
    depthPassState(*this);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    for (int face = 0; face < 6; ++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), m.cubeTex, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        QMatrix4x4 view;
        view.lookAt(at, at + kFaceDir[face], kFaceUp[face]);
        distanceProg_.setUniformValue("uLightVP", proj * view);
        drawShadowCasters(distanceProg_);
    }
    return true;
}

// Everything that casts a shadow, drawn with `program` (it needs uModel; the light's matrix is set):
// every rigid body (a box from the unit cube, a sphere or a hull from its mesh), the scenery while
// playing, and this frame's cloth and soft-body surfaces (uploadClothMesh put them in the buffer).
void Viewport::drawShadowCasters(QOpenGLShaderProgram& program) {
    auto draw = [&](const RenderSnapshot::Body& b) {
        if (b.shape == ShapeType::Box) {
            program.setUniformValue("uModel", bodyMatrix(b, b.halfExtents));
            cube_.vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, cube_.count);
            cube_.vao.release();
            return;
        }
        const bool sphere = b.shape == ShapeType::Sphere;
        if (!sphere && !b.mesh) return;
        GpuMesh& g = hullMesh(sphere ? unitSphere_ : b.mesh);
        program.setUniformValue("uModel", bodyMatrix(b, Vector3(sphere ? b.radius : 1.0f)));
        g.vao->bind();
        glDrawArrays(GL_TRIANGLES, 0, g.count);
        g.vao->release();
    };
    for (const auto& b : snap_->bodies) draw(b);
    if (!editMode_)
        for (const auto& b : scenery_) draw(b);
    if (clothRanges_.empty()) return;
    program.setUniformValue("uModel", QMatrix4x4());
    clothMesh_.vao->bind();
    for (const auto& [first, count] : clothRanges_) glDrawArrays(GL_TRIANGLES, first, count);
    clothMesh_.vao->release();
}

// ---------------------------------------------------------------------------
// The maps in the mesh shader
// ---------------------------------------------------------------------------
// Each slot's maps on units of their own, and what the shader needs to read them: the matrix from
// view space into the depth map's [0, 1] cube (`toTexture` maps clip space [-1, 1] there), a
// spotlight's lens (its depth made linear again), a lamp's position and reach.
void Viewport::bindShadowMaps(const QMatrix4x4& view) {
    static const char* mapNames[kMaxShadowLights] = {"uShadowMap0", "uShadowMap1", "uShadowMap2", "uShadowMap3"};
    static const char* cubeNames[kMaxShadowLights] = {"uShadowCube0", "uShadowCube1", "uShadowCube2", "uShadowCube3"};
    QMatrix4x4 toTexture;
    toTexture.translate(0.5f, 0.5f, 0.5f);
    toTexture.scale(0.5f);
    const QMatrix4x4 viewToWorld = view.inverted();
    QMatrix4x4 fromView[kMaxShadowLights];
    QVector2D lens[kMaxShadowLights];
    QVector3D lamp[kMaxShadowLights];
    float texel[kMaxShadowLights], reach[kMaxShadowLights];
    for (int s = 0; s < kMaxShadowLights; ++s) {
        const ShadowMap& m = shadowMaps_[s];
        const bool used = m.light >= 0;
        meshProg_.setUniformValue(mapNames[s], kFirstMapUnit + s);
        meshProg_.setUniformValue(cubeNames[s], kFirstCubeUnit + s);
        glActiveTexture(GLenum(GL_TEXTURE0 + kFirstMapUnit + s));
        glBindTexture(GL_TEXTURE_2D, used && !m.cube ? m.depth : 0);
        glActiveTexture(GLenum(GL_TEXTURE0 + kFirstCubeUnit + s));
        glBindTexture(GL_TEXTURE_CUBE_MAP, used && m.cube ? m.cubeTex : 0);
        fromView[s] = toTexture * m.viewProj * viewToWorld;
        lens[s] = m.cube ? QVector2D(0, 0) : QVector2D(m.nearClip, m.farClip);
        lamp[s] = qv(m.lampAt);
        texel[s] = 1.0f / float(kDepthMapSize);
        reach[s] = std::max(m.farClip, 1e-3f);
    }
    glActiveTexture(GL_TEXTURE0);
    meshProg_.setUniformValueArray("uShadowFromView", fromView, kMaxShadowLights);
    meshProg_.setUniformValueArray("uShadowLens", lens, kMaxShadowLights);
    meshProg_.setUniformValueArray("uShadowLamp", lamp, kMaxShadowLights);
    meshProg_.setUniformValueArray("uShadowTexel", texel, kMaxShadowLights, 1);
    meshProg_.setUniformValueArray("uShadowFar", reach, kMaxShadowLights, 1);
    meshProg_.setUniformValue("uViewToWorld", viewToWorld);
}

// ---------------------------------------------------------------------------
// The GPU targets, made once per slot when first needed
// ---------------------------------------------------------------------------
// A depth texture and a framebuffer that writes only depth into it.
bool Viewport::ensureDepthMap(int slot) {
    ShadowMap& m = shadowMaps_[slot];
    if (m.fbo) return true;
    glGenTextures(1, &m.depth);
    glBindTexture(GL_TEXTURE_2D, m.depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kDepthMapSize, kDepthMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &m.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m.depth, 0);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    if (!ok) qWarning("A shadow map framebuffer is incomplete: that light casts no shadow");
    return ok;
}

// A cube of single-float distances and a framebuffer that draws into one face at a time, with its own
// depth buffer so that each face keeps the nearest surface.
bool Viewport::ensureDistanceCube(int slot) {
    ShadowMap& m = shadowMaps_[slot];
    if (m.cubeFbo) return true;
    glGenTextures(1, &m.cubeTex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m.cubeTex);
    for (int face = 0; face < 6; ++face)
        glTexImage2D(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_R32F, kCubeFaceSize, kCubeFaceSize, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTexParameteri(GL_TEXTURE_CUBE_MAP, wrap, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glGenRenderbuffers(1, &m.cubeDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m.cubeDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kCubeFaceSize, kCubeFaceSize);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glGenFramebuffers(1, &m.cubeFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m.cubeFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, m.cubeTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m.cubeDepth);
    const GLenum colour = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &colour);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    if (!ok) qWarning("A lamp's shadow cube framebuffer is incomplete: that lamp casts no shadow");
    return ok;
}

// Every slot's GPU objects, freed (the context must be current: the destructor makes it so).
void Viewport::releaseShadowMaps() {
    for (ShadowMap& m : shadowMaps_) {
        if (m.depth) glDeleteTextures(1, &m.depth);
        if (m.fbo) glDeleteFramebuffers(1, &m.fbo);
        if (m.cubeTex) glDeleteTextures(1, &m.cubeTex);
        if (m.cubeDepth) glDeleteRenderbuffers(1, &m.cubeDepth);
        if (m.cubeFbo) glDeleteFramebuffers(1, &m.cubeFbo);
        m = ShadowMap();
    }
}
