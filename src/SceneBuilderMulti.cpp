// Several objects selected in the scene builder (see SceneBuilder.h): the inspector shows the active
// object, "—" in every field whose value differs between the selected (a checkbox half-checked), and
// the component cards of what all of them have. An edit is what differs from what the widgets showed
// when they were filled: only the field that was touched goes to every selected object, the others
// keep their own values. The shape and the components of an instance are its master's, so an edit
// there reaches the master and all its instances.
#include "SceneBuilder.h"

#include "RoleBar.h"

#include "ColliderPanel.h"
#include "InspectorWidgets.h"
#include "ObjectInspector.h"
#include "SoftPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>

#include <algorithm>

using namespace rf;

namespace {

// Copies what changed between `before` and `after` into `target`: the one field that was edited.
void take(float before, float after, float& target) {
    if (after != before) target = after;
}
void take(bool before, bool after, bool& target) {
    if (after != before) target = after;
}
void take(const Vector3& before, const Vector3& after, Vector3& target) {
    for (int k = 0; k < 3; ++k) take(before[k], after[k], target[k]);
}
void take(const std::string& before, const std::string& after, std::string& target) {
    if (after != before) target = after;
}

void takeObject(const SceneObject& before, const SceneObject& after, SceneObject& target) {
    take(before.name, after.name, target.name);
    take(before.visible, after.visible, target.visible);
    take(before.locked, after.locked, target.locked);
    take(before.color, after.color, target.color);
    take(before.position, after.position, target.position);
    take(before.rotationDeg, after.rotationDeg, target.rotationDeg);
}

void takeCollider(const ColliderRole& before, const ColliderRole& after, ColliderRole& target) {
    if (after.kind != before.kind) target.kind = after.kind;
    take(before.fitToGeometry, after.fitToGeometry, target.fitToGeometry);
    take(before.size, after.size, target.size);
    take(before.offset, after.offset, target.offset);
    take(before.rotationDeg, after.rotationDeg, target.rotationDeg);
}

// The geometry and the numbers of the components (the components themselves are switched by
// toggleRole, not by these widgets).
void takeShape(const Entity& before, const Entity& after, Entity& t) {
    if (after.shape != before.shape) t.shape = after.shape;
    take(before.size, after.size, t.size);
    take(before.rigid.density, after.rigid.density, t.rigid.density);
    take(before.rigid.friction, after.rigid.friction, t.rigid.friction);
    take(before.rigid.restitution, after.rigid.restitution, t.rigid.restitution);
    take(before.rigid.fixed, after.rigid.fixed, t.rigid.fixed);
    take(before.rigid.velocity, after.rigid.velocity, t.rigid.velocity);
    take(before.rigid.angularVelocity, after.rigid.angularVelocity, t.rigid.angularVelocity);
    takeCollider(before.collider, after.collider, t.collider);
    take(before.soft.density, after.soft.density, t.soft.density);
    take(before.soft.youngModulus, after.soft.youngModulus, t.soft.youngModulus);
    take(before.soft.poissonRatio, after.soft.poissonRatio, t.soft.poissonRatio);
    take(before.soft.friction, after.soft.friction, t.soft.friction);
    take(before.cloth.areaDensity, after.cloth.areaDensity, t.cloth.areaDensity);
    take(before.cloth.bendCompliance, after.cloth.bendCompliance, t.cloth.bendCompliance);
    take(before.cloth.tearable, after.cloth.tearable, t.cloth.tearable);
    const int flipped = before.cloth.pinnedEdges ^ after.cloth.pinnedEdges; // the edges whose box was clicked
    t.cloth.pinnedEdges = (t.cloth.pinnedEdges & ~flipped) | (after.cloth.pinnedEdges & flipped);
    take(before.magnet.moment, after.magnet.moment, t.magnet.moment);
    take(before.emitter.smoke, after.emitter.smoke, t.emitter.smoke);
    take(before.emitter.temperature, after.emitter.temperature, t.emitter.temperature);
    take(before.emitter.liquid, after.emitter.liquid, t.emitter.liquid);
    take(before.emitter.velocity, after.emitter.velocity, t.emitter.velocity);
    take(before.heat.temperature, after.heat.temperature, t.heat.temperature);
    take(before.heat.smoke, after.heat.smoke, t.heat.smoke);
}

// "—" in a number field when the selected differ in it.
void markNumber(QDoubleSpinBox* s, const std::vector<Entity>& all, float (*get)(const Entity&)) {
    for (const Entity& e : all)
        if (get(e) != get(all.front())) return showMixed(s);
}
void markVector(Vec3Row* r, const std::vector<Entity>& all, Vector3 (*get)(const Entity&)) {
    for (int k = 0; k < 3; ++k)
        for (const Entity& e : all)
            if (get(e)[k] != get(all.front())[k]) {
                showMixed(r->spin(k));
                break;
            }
}
void markCheck(QCheckBox* c, const std::vector<Entity>& all, bool (*get)(const Entity&)) {
    for (const Entity& e : all)
        if (get(e) != get(all.front())) { // unchecked, and says so: a click checks it for all
            const QSignalBlocker quiet(c);
            c->setProperty("realText", c->text());
            c->setText(c->text() + "  (—: у всех разное)");
            c->setChecked(false);
            return;
        }
}

} // namespace

std::vector<Entity*> SceneBuilder::selectedEntities() {
    std::vector<Entity*> out;
    for (uint32_t id : selection_)
        if (Entity* e = entityById(id)) out.push_back(e);
    return out;
}

std::vector<RoleIcon> SceneBuilder::commonRoles() const {
    std::vector<RoleIcon> roles;
    std::vector<Entity> shapes;
    for (uint32_t id : selection_)
        if (indexOf(id) >= 0) shapes.push_back(resolveInstance(graph_, graph_.entities[size_t(indexOf(id))]));
    if (shapes.empty() || shapes.size() != selection_.size()) return roles;
    for (int k = 0; k < int(RoleIcon::Count); ++k)
        if (std::all_of(shapes.begin(), shapes.end(), [k](const Entity& e) { return roleEnabled(e, RoleIcon(k)); })) roles.push_back(RoleIcon(k));
    return roles;
}

Entity SceneBuilder::readWidgets() const {
    Entity e = shown_; // the id and whatever no widget shows
    object_->writeTo(e);
    readDetails(e);
    return e;
}

// Called after the widgets were filled from the active object.
void SceneBuilder::markMixed() {
    std::vector<const SceneObject*> objects;
    for (uint32_t id : selection_) objects.push_back(findObject(graph_, id));
    int position = 0, rotation = 0;
    bool name = false;
    for (const SceneObject* o : objects) {
        name = name || o->name != objects.front()->name;
        for (int k = 0; k < 3; ++k) {
            position |= o->position[k] != objects.front()->position[k] ? 1 << k : 0;
            rotation |= o->rotationDeg[k] != objects.front()->rotationDeg[k] ? 1 << k : 0;
        }
    }
    object_->showMixed(name, position, rotation);
    std::vector<Entity> all;
    for (uint32_t id : selection_)
        if (const Entity* e = entityById(id)) all.push_back(resolveInstance(graph_, *e));
    if (all.size() != selection_.size()) return; // not all shapes: only the object's part is shown
    if (std::any_of(all.begin(), all.end(), [&](const Entity& e) { return e.shape != all.front().shape; })) shape_->setCurrentIndex(-1);
    markVector(size_, all, [](const Entity& e) { return e.size; });
    markNumber(rigidDensity_, all, [](const Entity& e) { return e.rigid.density; });
    markNumber(friction_, all, [](const Entity& e) { return e.rigid.friction; });
    markNumber(restitution_, all, [](const Entity& e) { return e.rigid.restitution; });
    markCheck(fixed_, all, [](const Entity& e) { return e.rigid.fixed; });
    markVector(velocity_, all, [](const Entity& e) { return e.rigid.velocity; });
    markVector(spin_, all, [](const Entity& e) { return e.rigid.angularVelocity; });
    markNumber(soft_->densityField(), all, [](const Entity& e) { return e.soft.density; });
    markNumber(soft_->youngField(), all, [](const Entity& e) { return e.soft.youngModulus; });
    markNumber(soft_->poissonField(), all, [](const Entity& e) { return e.soft.poissonRatio; });
    markNumber(soft_->frictionField(), all, [](const Entity& e) { return e.soft.friction; });
    if (std::any_of(all.begin(), all.end(), [&](const Entity& e) { return softPresetOf(e.soft) != softPresetOf(all.front().soft); }))
        soft_->showMixedPreset(); // of different materials: no button pressed, a click makes them all one
    markNumber(clothDensity_, all, [](const Entity& e) { return e.cloth.areaDensity; });
    markNumber(bend_, all, [](const Entity& e) { return e.cloth.bendCompliance; });
    markCheck(tearable_, all, [](const Entity& e) { return e.cloth.tearable; });
    markVector(moment_, all, [](const Entity& e) { return e.magnet.moment; });
    markNumber(emitSmoke_, all, [](const Entity& e) { return e.emitter.smoke; });
    markNumber(emitTemperature_, all, [](const Entity& e) { return e.emitter.temperature; });
    markNumber(emitLiquid_, all, [](const Entity& e) { return e.emitter.liquid; });
    markVector(emitVelocity_, all, [](const Entity& e) { return e.emitter.velocity; });
    markNumber(heatTemperature_, all, [](const Entity& e) { return e.heat.temperature; });
    markNumber(heatSmoke_, all, [](const Entity& e) { return e.heat.smoke; });
}

// A widget changed: what differs from what was shown goes to every selected object - its own part
// (name, pose, eye, lock) to the object, the shape's part to the shape's master.
void SceneBuilder::applyWidgetEdit(bool details) {
    (void)details; // the object card and the details are compared alike
    if (filling_ || selection_.empty()) return;
    const Entity after = readWidgets();
    prepareEdit(selectedId_);
    remember(true);
    for (uint32_t id : selection_) {
        SceneObject* o = objectById(id);
        if (!o) continue;
        takeObject(shown_, after, *o);
        if (entityById(id)) takeShape(shown_, after, *entityById(masterOf(id)));
    }
    shown_ = after;
    refreshList();
    if (selection_.size() > 1) fillInspector(); // a field all now share shows its number again
    else refreshHeader();
    applyEdit(selectedId_);
}
