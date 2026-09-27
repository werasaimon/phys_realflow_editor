// Auto-smooth normals for the viewer (see MeshNormals.h).
#include "MeshNormals.h"

#include "math/Scalar.h"

#include <array>
#include <cmath>
#include <map>

using rf::Vector3;

namespace {

// For every vertex, the one index all vertices at the same place share (the first of them).
std::vector<uint32_t> weldByPosition(const rf::TriMesh& m) {
    std::map<std::array<float, 3>, uint32_t> first;
    std::vector<uint32_t> welded(m.positions.size());
    for (uint32_t i = 0; i < uint32_t(m.positions.size()); ++i) {
        const Vector3& p = m.positions[i];
        welded[i] = first.emplace(std::array<float, 3>{p.x, p.y, p.z}, i).first->second;
    }
    return welded;
}

} // namespace

std::vector<Vector3> autoSmoothNormals(const rf::TriMesh& m, float creaseDegrees) {
    const size_t triangles = m.triangles.size();
    const std::vector<uint32_t> welded = weldByPosition(m);
    // Each face's unit normal, its normal times its area (big faces weigh more), and which faces
    // touch each (welded) point.
    std::vector<Vector3> unit(triangles), weighted(triangles);
    std::vector<std::vector<uint32_t>> facesAt(m.positions.size());
    for (size_t t = 0; t < triangles; ++t) {
        const auto& tri = m.triangles[t];
        const Vector3 c = rf::cross(m.positions[tri[1]] - m.positions[tri[0]], m.positions[tri[2]] - m.positions[tri[0]]);
        weighted[t] = c * 0.5f;
        unit[t] = rf::normalize(c);
        for (int k = 0; k < 3; ++k) facesAt[welded[tri[k]]].push_back(uint32_t(t));
    }
    // A corner: the faces at its point that bend less than the crease angle from its own face.
    const float creaseCos = std::cos(creaseDegrees * rf::kPi / 180.0f);
    std::vector<Vector3> normals(3 * triangles);
    for (size_t t = 0; t < triangles; ++t)
        for (int k = 0; k < 3; ++k) {
            Vector3 sum(0.0f);
            for (uint32_t f : facesAt[welded[m.triangles[t][k]]])
                if (rf::dot(unit[f], unit[t]) > creaseCos) sum += weighted[f];
            const Vector3 n = rf::normalize(sum);
            normals[3 * t + size_t(k)] = rf::length2(n) > 0 ? n : unit[t];
        }
    return normals;
}
