// "Сохранить из симуляции" (K), as Unreal's "Keep Simulation Changes" (see SceneBuilder.h). Play
// and Stop work as in Unity: whatever the simulation did is gone after Stop. K keeps it: the live
// poses of the selected objects (of all, when nothing is selected) become their poses in the scene
// that Stop brings back. After Stop, one Ctrl+Z puts them back where they were before K.
//
// The live pose is read on the simulation's thread from the objects' meta-objects: a rigid body's
// pose turned back into the object's (entity = body pose - rotation * bodyOffset, as GraphScene
// reads it), a particle group's centre (its rotation stays). An array's copies and glued groups
// are not kept (their poses are not their own).
#include "SceneBuilder.h"

#include "RuPlural.h"

#include <QPointer>

using namespace rf;

namespace {

// Where the object `id` is now in the simulation (false: it has nothing that moves).
bool livePose(const Simulation& s, const GraphScene& scene, uint32_t id, Vector3& position, Quaternion& rotation, bool& turned) {
    for (const MetaObject& m : scene.metaObjects(id)) {
        if (m.kind == MetaObject::Kind::RigidBody && s.rigid.isAlive(m.handle)) {
            const RigidBody& b = s.rigid.bodies()[size_t(m.handle)];
            rotation = (b.rot * m.bodyToEntity).normalized();
            position = b.pos - rotation.rotate(m.bodyOffset); // the object's centre, not the collider's
            turned = true;
            return true;
        }
        if (m.kind == MetaObject::Kind::SoftBody || m.kind == MetaObject::Kind::Liquid || m.kind == MetaObject::Kind::Cloth) {
            Vector3 sum(0.0f);
            int n = 0;
            for (size_t i = 0; i < s.particles.size(); ++i)
                if (s.particles.groupOf(int(i)) == m.handle) sum += s.particles.positions()[i], ++n;
            if (n == 0) return false;
            position = sum / float(n);
            turned = false;
            return true;
        }
    }
    return false;
}

} // namespace

void SceneBuilder::keepSimulationPoses() {
    if (editing() || sample_ || !hasPlayBackup_) return;
    std::vector<uint32_t> ids; // the selected objects' shapes, or every shape
    for (const Entity& e : graph_.entities) {
        bool wanted = selection_.empty();
        for (uint32_t s : selection_) wanted |= isInside(e.id, s);
        if (wanted) ids.push_back(e.id);
    }
    QPointer<SceneBuilder> self(this);
    ctrl_->post([ids, self](Simulation& s) {
        auto* scene = dynamic_cast<GraphScene*>(s.scene());
        if (!scene) return;
        std::vector<KeptPose> poses;
        for (uint32_t id : ids) {
            KeptPose k;
            k.id = id;
            if (livePose(s, *scene, id, k.position, k.rotation, k.turned)) poses.push_back(k);
        }
        QMetaObject::invokeMethod(self, [self, poses] {
            if (self) self->applyKeptPoses(poses);
        }, Qt::QueuedConnection);
    });
}

// Back from the simulation: the poses go into the scene Stop brings back (and into the graphs of
// the running scene, so the next edit does not look like a move).
void SceneBuilder::applyKeptPoses(const std::vector<KeptPose>& poses) {
    if (editing() || !hasPlayBackup_) return; // stopped in the meantime
    if (!hasKeptBefore_) { // the first K of this play: the undo step Stop will add
        keptBefore_ = playBackup_;
        hasKeptBefore_ = true;
    }
    for (const KeptPose& k : poses)
        for (SceneGraph* g : {&playBackup_, &graph_, &simGraph_}) {
            SceneObject* o = const_cast<SceneObject*>(findObject(*g, k.id)); // our own graph: ours to change
            if (!o) continue;
            Vector3 p;
            Quaternion q;
            worldPose(*g, *o, p, q);
            const Vector3 keepDegrees = o->rotationDeg;
            setWorldPose(*g, *o, k.position, k.turned ? k.rotation : q);
            if (!k.turned) o->rotationDeg = keepDegrees; // a particle shape keeps its angles exactly
        }
    if (const SceneObject* o = selectedObject()) showTransform(*o);
    emit statusMessage(QString("Сохранено из симуляции: %1 — после ■ Стоп останутся здесь (Ctrl+Z после Стоп вернёт)")
                           .arg(ruPlural(static_cast<long long>(poses.size()), "объект", "объекта", "объектов")));
    emit simulationKept(int(poses.size()));
}
