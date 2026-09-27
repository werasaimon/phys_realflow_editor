// The scene graph drawn and picked as authored (see EditView.h).
#include "EditView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace rf;

namespace {

bool madeOfSomething(const Entity& e) { return e.rigid.enabled || e.soft.enabled || e.liquid.enabled || e.cloth.enabled; }

// Geometry only (no role) is drawn muted: the colour half-way to a cool grey.
Vector3 displayColor(const Entity& e) {
    if (madeOfSomething(e) || e.magnet.enabled || e.emitter.enabled || e.flammable.enabled || e.heat.enabled) return e.color;
    return e.color * 0.55f + Vector3(0.56f, 0.59f, 0.64f) * 0.45f;
}

std::string sourceKey(const Entity& e) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%d %.6g %.6g %.6g ", int(e.shape), double(e.size.x), double(e.size.y), double(e.size.z));
    return buf + e.meshFile;
}

} // namespace

bool rayTriangle(const Vector3& o, const Vector3& d, const Vector3& a, const Vector3& b, const Vector3& c, float& t) {
    const Vector3 e1 = b - a, e2 = c - a, p = cross(d, e2);
    const float det = dot(e1, p);
    if (std::fabs(det) < 1e-12f) return false; // the ray runs along the triangle
    const float inv = 1.0f / det;
    const Vector3 s = o - a;
    const float u = dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    const Vector3 q = cross(s, e1);
    const float v = dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = dot(e2, q) * inv;
    return t > 0;
}

const EditView::Source& EditView::source(const SceneGraph& g, const Entity& e) {
    const std::string key = sourceKey(e);
    if (cache_.size() > 512 && !cache_.count(key)) cache_.clear(); // sizes left behind by scale drags
    Source& s = cache_[key];
    if (s.mesh) return s;
    s.key = key;
    s.mesh = std::make_shared<const TriMesh>(entityLocalMesh(e, g.baseDirectory));
    s.bounds = s.mesh->bounds();
    s.bvh.reset();
    if (s.mesh->triangles.size() > 2000) {
        s.bvh = std::make_shared<MeshBVH>();
        s.bvh->build(*s.mesh);
    }
    return s;
}

std::shared_ptr<RenderSnapshot> EditView::snapshot(const SceneGraph& g, const std::string& name, std::vector<uint32_t>& bodyEntity) {
    auto snap = std::make_shared<RenderSnapshot>();
    snap->mode = SimMode::Rigid;
    const Vector3 w = g.world.size;
    snap->domain = AABB({-0.5f * w.x, 0.0f, -0.5f * w.z}, {0.5f * w.x, w.y, 0.5f * w.z});
    snap->sceneName = name;
    describeLights(g, *snap); // the scene's lights shade the authored scene as they will the played one
    bodyEntity.clear();
    for (const Entity& e : worldEntities(g)) {
        if (!e.visible) continue;
        const Source& s = source(g, e);
        if (s.mesh->triangles.empty()) continue; // an unreadable model file
        RenderSnapshot::Body b;
        // The sphere gets smooth normals (the viewer draws "compound" meshes smooth); everything else,
        // models included, keeps its facets - a low-poly model smoothed looks like a blob.
        const bool smooth = e.shape == ShapeKind::Sphere;
        b.shape = smooth ? ShapeType::Compound : ShapeType::ConvexHull;
        b.pos = e.position;
        b.rot = entityRotation(e);
        b.halfExtents = s.bounds.extent() * 0.5f;
        b.radius = 0.5f * length(s.bounds.extent());
        b.color = displayColor(e);
        b.mesh = s.mesh;
        b.movable = false;
        snap->bodies.push_back(b);
        bodyEntity.push_back(e.id);
    }
    return snap;
}

bool EditView::raycastLocal(const Source& s, const Vector3& o, const Vector3& d, float& t) const {
    Ray local{o, d};
    float tNear, tFar;
    if (!intersectBox(local, s.bounds, tNear, tFar)) return false;
    if (s.bvh) {
        RayHit hit;
        if (!s.bvh->raycast(o, d, kInf, hit)) return false;
        t = hit.t;
        return true;
    }
    t = kInf;
    const TriMesh& m = *s.mesh;
    for (const auto& tri : m.triangles) {
        float ti;
        if (rayTriangle(o, d, m.positions[tri[0]], m.positions[tri[1]], m.positions[tri[2]], ti) && ti < t) t = ti;
    }
    return t < kInf;
}

std::vector<PickHit> EditView::pickAll(const SceneGraph& g, const Ray& ray) {
    std::vector<PickHit> hits;
    for (const Entity& e : worldEntities(g)) {
        if (!e.visible || e.locked) continue;
        const Source& s = source(g, e);
        if (s.mesh->triangles.empty()) continue;
        const Matrix3x3 Rt = entityRotation(e).toMatrix3x3().transposed(); // the ray in the entity's frame
        float t;
        if (raycastLocal(s, Rt * (ray.origin - e.position), Rt * ray.dir, t)) hits.push_back({e.id, t, ray.at(t), e.name});
    }
    std::sort(hits.begin(), hits.end(), [](const PickHit& a, const PickHit& b) { return a.t < b.t; });
    return hits;
}

AABB EditView::worldBounds(const SceneGraph& g, const Entity& e) {
    const Source& s = source(g, e);
    const Matrix3x3 R = entityRotation(e).toMatrix3x3();
    AABB box;
    for (int k = 0; k < 8; ++k) {
        const Vector3 c(k & 1 ? s.bounds.hi.x : s.bounds.lo.x, k & 2 ? s.bounds.hi.y : s.bounds.lo.y, k & 4 ? s.bounds.hi.z : s.bounds.lo.z);
        box.expand(e.position + R * c);
    }
    return box;
}
