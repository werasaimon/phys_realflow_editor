// Guide lines of colliders (see ColliderGuides.h): per shape kind in the shape's own frame, cached,
// then placed where the body or the object is.
#include "ColliderGuides.h"

#include <cmath>
#include <utility>

using namespace rf;

namespace {

constexpr int kCircle = 48; // segments of a full circle: smooth at any zoom, cheap

// A circle of radius r around `centre` in the plane spanned by the unit axes u and v.
void appendCircle(std::vector<Vector3>& out, const Vector3& centre, const Vector3& u, const Vector3& v, float r,
                  float from = 0.0f, float to = 2.0f * kPi, int segments = kCircle) {
    Vector3 prev = centre + (u * std::cos(from) + v * std::sin(from)) * r;
    for (int k = 1; k <= segments; ++k) {
        const float a = from + (to - from) * float(k) / float(segments);
        const Vector3 next = centre + (u * std::cos(a) + v * std::sin(a)) * r;
        out.push_back(prev);
        out.push_back(next);
        prev = next;
    }
}

// Lines of a shape in its own frame, whatever it is.
std::vector<Vector3> linesOf(const ConvexShape& s) {
    switch (s.type()) {
    case ShapeType::Box: return boxGuide(static_cast<const BoxShape&>(s).halfExtents());
    case ShapeType::Sphere: return sphereGuide(static_cast<const SphereShape&>(s).radius());
    case ShapeType::Capsule: {
        const auto& c = static_cast<const CapsuleShape&>(s);
        return capsuleGuide(c.radius(), c.halfHeight());
    }
    case ShapeType::ConvexHull: return meshEdgeGuide(*static_cast<const ConvexHullShape&>(s).mesh());
    case ShapeType::Compound: {
        std::vector<Vector3> all;
        for (const auto& part : static_cast<const CompoundShape&>(s).partMeshes()) {
            const std::vector<Vector3> lines = meshEdgeGuide(*part); // parts are in the compound's frame already
            all.insert(all.end(), lines.begin(), lines.end());
        }
        return all;
    }
    case ShapeType::Triangle: break;
    }
    return {};
}

} // namespace

std::vector<Vector3> boxGuide(const Vector3& h) {
    std::vector<Vector3> out;
    const Vector3 c[8] = {{-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z},
                          {-h.x, -h.y, h.z},  {h.x, -h.y, h.z},  {h.x, h.y, h.z},  {-h.x, h.y, h.z}};
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) {
        out.push_back(c[e[0]]);
        out.push_back(c[e[1]]);
    }
    return out;
}

std::vector<Vector3> sphereGuide(float r) {
    std::vector<Vector3> out;
    const Vector3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1), o(0.0f);
    appendCircle(out, o, x, z, r); // equator
    appendCircle(out, o, x, y, r); // two meridians
    appendCircle(out, o, z, y, r);
    return out;
}

// A capsule standing on the y axis: a ring where each cap meets the side, four side lines, and the
// caps as half circles in the xy and zy planes.
std::vector<Vector3> capsuleGuide(float r, float h) {
    std::vector<Vector3> out;
    const Vector3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1), top(0, h, 0), bottom(0, -h, 0);
    appendCircle(out, top, x, z, r);
    appendCircle(out, bottom, x, z, r);
    for (const Vector3& side : {x * r, x * -r, z * r, z * -r}) {
        out.push_back(bottom + side);
        out.push_back(top + side);
    }
    for (const Vector3& u : {x, z}) {
        appendCircle(out, top, u, y, r, 0.0f, kPi, kCircle / 2);
        appendCircle(out, bottom, u, y, r, kPi, 2.0f * kPi, kCircle / 2);
    }
    return out;
}

// The real edges of a polyhedron: every edge whose two triangles are not in one plane (and edges
// with only one triangle). A flat face split into triangles draws only its outline.
std::vector<Vector3> meshEdgeGuide(const TriMesh& mesh) {
    std::map<std::pair<uint32_t, uint32_t>, std::vector<size_t>> edgeFaces;
    for (size_t t = 0; t < mesh.triangles.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            uint32_t a = mesh.triangles[t][size_t(k)], b = mesh.triangles[t][size_t((k + 1) % 3)];
            if (a > b) std::swap(a, b);
            edgeFaces[{a, b}].push_back(t);
        }
    std::vector<Vector3> out;
    for (const auto& [edge, faces] : edgeFaces) {
        const bool bends = faces.size() != 2 || dot(mesh.faceNormal(faces[0]), mesh.faceNormal(faces[1])) < 0.9998f; // > ~1°
        if (!bends) continue;
        out.push_back(mesh.positions[edge.first]);
        out.push_back(mesh.positions[edge.second]);
    }
    return out;
}

const std::vector<Vector3>& ColliderGuides::localLines(const std::shared_ptr<const ConvexShape>& shape) {
    auto it = cache_.find(shape.get());
    if (it == cache_.end()) it = cache_.emplace(shape.get(), Entry{shape, linesOf(*shape)}).first;
    return it->second.lines;
}

void ColliderGuides::append(const std::shared_ptr<const ConvexShape>& shape, const Vector3& position,
                            const Quaternion& rotation, State state, std::vector<float>& lines) {
    if (!shape) return;
    const Matrix3x3 R = rotation.toMatrix3x3();
    for (const Vector3& p : localLines(shape)) {
        const Vector3 w = position + R * p;
        lines.insert(lines.end(), {w.x, w.y, w.z, float(state)}); // the viewport's line layout: x y z scalar
    }
}

void ColliderGuides::prune() {
    for (auto it = cache_.begin(); it != cache_.end();)
        it = it->second.shape.use_count() == 1 ? cache_.erase(it) : std::next(it);
}
