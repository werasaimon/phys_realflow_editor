#pragma once
// The scene while it is being edited: the scene graph itself, drawn as it was authored, with no
// simulation running. The editor has three layers (the user's rule):
//   SOURCES      - geometry: a box, a sphere, a model file; physics never destroys it;
//   OBJECTS      - an entity: a source with a pose and roles (what the gizmo and inspector edit);
//   META-OBJECTS - what a role makes when the scene plays: a rigid body, a cloth, an emitter.
// In edit mode only the first two exist, and this class shows them: one drawn body per visible
// entity (its shape's mesh at its pose), and the mouse ray tested against that same mesh, so a
// click selects exactly what is drawn under the cursor.
//
// Picking: the ray is taken into the entity's own frame (the mesh is cached there, rebuilt only
// when the shape, size or model file changes), tested against the mesh's box first, then against
// its triangles (Moller-Trumbore; a BVH for models of more than 2000 triangles). Nearest hit wins.
#include "OrbitCamera.h"
#include "scene/SceneGraph.h"
#include "scene/Simulation.h"
#include "spatial/BVH.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// One object on a ray: its id, the distance along the ray, where the ray met it, and its name (for
// the "2 / 3: Штора" hint when several objects lie on the ray).
struct PickHit {
    uint32_t id = 0;
    float t = 0;
    rf::Vector3 point;
    std::string name;
};

class EditView {
public:
    // The snapshot the viewport draws in edit mode; bodyEntity[i] = id of the entity of body i.
    std::shared_ptr<rf::RenderSnapshot> snapshot(const rf::SceneGraph& g, const std::string& name,
                                                 std::vector<uint32_t>& bodyEntity);
    // Every visible, unlocked entity the ray passes through, nearest first.
    std::vector<PickHit> pickAll(const rf::SceneGraph& g, const Ray& ray);
    // The entity's box in the world (its mesh's box turned and moved).
    rf::AABB worldBounds(const rf::SceneGraph& g, const rf::Entity& e);

private:
    struct Source {
        std::string key; // shape, size and model file: the cache is rebuilt when it changes
        std::shared_ptr<const rf::TriMesh> mesh;
        rf::AABB bounds;
        std::shared_ptr<rf::MeshBVH> bvh; // big models only
    };
    const Source& source(const rf::SceneGraph& g, const rf::Entity& e);
    bool raycastLocal(const Source& s, const rf::Vector3& o, const rf::Vector3& d, float& t) const;

    std::map<uint32_t, Source> cache_; // by entity id
};

// Moller-Trumbore ray / triangle: the distance t along the ray, false if missed or behind.
bool rayTriangle(const rf::Vector3& o, const rf::Vector3& d, const rf::Vector3& a, const rf::Vector3& b,
                 const rf::Vector3& c, float& t);
