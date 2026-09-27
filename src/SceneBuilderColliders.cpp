// The collider wireframes of the scene builder (see SceneBuilder.h and ColliderGuides.h): in edit
// mode made from the graph - each entity's collider placed where the entity stands -, while the
// scene plays made from the simulation's bodies - the body's pose is the collider's pose, so the
// barrel outline rolls around the sphere that it moves. The selected object always shows its
// collider; the toolbar's "Коллайдеры" shows every object's.
#include "SceneBuilder.h"

#include <QString>

using namespace rf;

namespace {

// What an entity's collider depends on (not its pose): the cache key of its shape.
std::string colliderKey(const Entity& e) {
    const ColliderRole& c = e.collider;
    return QString::asprintf("%d %g %g %g %s | %d %d %g %g %g %g %g %g %g %g %g", int(e.shape), e.size.x, e.size.y, e.size.z,
                             e.meshFile.c_str(), int(c.kind), int(c.fitToGeometry), c.size.x, c.size.y, c.size.z, c.offset.x,
                             c.offset.y, c.offset.z, c.rotationDeg.x, c.rotationDeg.y, c.rotationDeg.z)
        .toStdString();
}

} // namespace

const EntityCollider& SceneBuilder::cachedCollider(const Entity& e) {
    auto& cached = editColliders_[e.id];
    const std::string key = colliderKey(e);
    if (cached.first != key || !cached.second.shape) cached = {key, entityCollider(e, graph_.baseDirectory)};
    return cached.second;
}

void SceneBuilder::setShowAllColliders(bool on) {
    showAllColliders_ = on;
    refreshColliderGuides();
}

void SceneBuilder::refreshColliderGuides() {
    std::vector<float> lines;
    if (!editing() || sample_) { // play mode: the next snapshot brings them (playColliderGuides)
        if (sample_) emit colliderGuides(lines);
        return;
    }
    for (const Entity& e : graph_.entities) {
        if (!e.visible || e.locked || !e.collider.enabled || (!showAllColliders_ && e.id != selectedId_)) continue;
        const EntityCollider& c = cachedCollider(e);
        const Quaternion q = entityRotation(e);
        const bool moves = e.rigid.enabled && !e.rigid.fixed && e.shape != ShapeKind::Plane;
        guides_.append(c.shape, e.position + q.toMatrix3x3() * c.position, q * c.rotation,
                       moves ? ColliderGuides::Dynamic : ColliderGuides::Static, lines);
    }
    guides_.prune();
    emit colliderGuides(std::move(lines));
}

// While the scene plays: every body is its collider (the snapshot's collision shape at the body's
// pose); only the bodies of the selected object unless all are asked for.
void SceneBuilder::playColliderGuides(const RenderSnapshot& s) {
    std::vector<float> lines;
    for (size_t b = 0; b < s.bodies.size(); ++b) {
        const RenderSnapshot::Body& body = s.bodies[b];
        const uint32_t id = b < bodyEntity_.size() ? bodyEntity_[b] : 0;
        if (!body.collisionShape || id == 0 || (!showAllColliders_ && id != selectedId_)) continue;
        const int i = indexOf(id);
        if (i < 0 || !graph_.entities[size_t(i)].collider.enabled || graph_.entities[size_t(i)].locked) continue;
        const ColliderGuides::State state = !body.movable ? ColliderGuides::Static
                                            : body.sleeping ? ColliderGuides::Sleeping
                                                            : ColliderGuides::Dynamic;
        guides_.append(body.collisionShape, body.pos, body.rot, state, lines);
    }
    emit colliderGuides(std::move(lines));
}
