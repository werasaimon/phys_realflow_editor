// Shift + gizmo = clone, as in 3ds Max (see SceneBuilder.h and docs/controls.md). Shift held when a
// gizmo drag starts: the selected objects stay, a copy of each follows the gizmo. When the button is
// released the popover asks how many copies and of what kind; the copies then repeat the drag's step
// from the original: copy k is moved and turned k times as the dragged copy was (a move d: d, 2d,
// 3d...; a turn a about the gizmo's centre: a, 2a...), and scaled by the factors to the k-th power.
//   Копия     - independent shapes (groups with all they hold);
//   Экземпляр - shapes sharing the original's geometry and components (instanceOf);
//   Массив    - one array per original shape: a row with the drag's step (and turn), the original its
//               hidden template, so one number changes how many there are.
// The drag, the popover and the copies are one undo step; Esc or Отмена leaves the scene as it was.
#include "SceneBuilder.h"
#include "RuPlural.h"

#include <cmath>

using namespace rf;

void SceneBuilder::onCloneDragStarted() {
    if (!gizmoEntity_ || gizmoStarts_.empty()) return;
    cloneDrag_ = true;
    cloneOf_.clear();
    std::map<uint32_t, GizmoStart> moving;
    std::vector<uint32_t> copies;
    uint32_t activeCopy = 0;
    for (const auto& [id, start] : gizmoStarts_) {
        const uint32_t c = copySubtree(id, false, objectById(id)->parent);
        cloneOf_.push_back({id, c});
        moving[c] = start;
        copies.push_back(c);
        if (id == gizmoEntity_) activeCopy = c;
    }
    gizmoStarts_ = moving;
    gizmoEntity_ = activeCopy ? activeCopy : copies.front();
    refreshList();
    setSelection(copies, gizmoEntity_);
}

bool SceneBuilder::cloneAllows(CloneKind kind) const {
    // An instance shares its master's size and an array's copies its template's: neither can grow.
    return kind == CloneKind::Copy || lastPose_.mode != GizmoMode::Scale;
}

// Copy k of an original: M^k applied to its start pose, M being the drag (from -> to) as a move and
// a turn in the world; its size times the scale factors to the k-th power.
uint32_t SceneBuilder::placeClone(uint32_t original, const CloneStep& step, int k, bool instance) {
    const uint32_t c = copySubtree(original, instance, objectById(original)->parent);
    SceneObject* o = objectById(c);
    o->name += " (" + std::to_string(k) + ")";
    const Quaternion turn = (step.toRotation * step.fromRotation.conjugate()).normalized();
    Vector3 p = step.from;
    Quaternion q = step.fromRotation;
    for (int i = 0; i < k; ++i) {
        p = step.to + turn.rotate(p - step.from);
        q = (turn * q).normalized();
    }
    const Vector3 keep = o->rotationDeg;
    setWorldPose(graph_, *o, p, q);
    if (!step.turns) o->rotationDeg = keep; // a move: the angles stay exactly as they were
    if (step.scale.x != 1.0f || step.scale.y != 1.0f || step.scale.z != 1.0f) {
        const Vector3 f(std::pow(step.scale.x, float(k)), std::pow(step.scale.y, float(k)), std::pow(step.scale.z, float(k)));
        scaleFrom(graph_, c, f); // the copy is new: its sizes are the original's
    }
    return c;
}

// One array for the original shape: a row of copies+1 (the original's place included), the drag's
// move as the step and its turn as the turn per copy; the original becomes the hidden template.
uint32_t SceneBuilder::cloneAsArray(uint32_t original, const CloneStep& step, int copies) {
    Entity& t = *entityById(original);
    ArrayObject a;
    a.id = newId();
    a.templateId = original;
    a.name = "Массив: " + t.name;
    a.parent = t.parent;
    a.color = t.color;
    a.pattern = ArrayPattern::Line;
    a.count[0] = copies + 1;
    a.step = step.to - step.from;
    a.rotationStepDeg = step.turns ? eulerDegrees((step.toRotation * step.fromRotation.conjugate()).normalized()) : Vector3(0.0f);
    setWorldPose(graph_, a, step.from, Quaternion());
    t.visible = false;
    graph_.arrays.push_back(a);
    return a.id;
}

// The popover's OK: the originals go back where they were, the copies are made from each one's
// step. The dragged copy was only a preview.
void SceneBuilder::finishClone(int copies, CloneKind kind) {
    if (!clonePending_) return;
    clonePending_ = false;
    const SceneGraph dragged = graph_;
    graph_ = gizmoGraph_; // the scene as before the drag (the undo step already holds it)
    std::vector<uint32_t> made;
    for (const auto& [original, preview] : cloneOf_) {
        CloneStep step;
        worldPose(gizmoGraph_, *findObject(gizmoGraph_, original), step.from, step.fromRotation);
        worldPose(dragged, *findObject(dragged, preview), step.to, step.toRotation);
        step.turns = lastPose_.mode == GizmoMode::Rotate;
        if (lastPose_.mode == GizmoMode::Scale) step.scale = lastPose_.scale;
        if (kind == CloneKind::Array && entityById(original) && cloneAllows(kind)) {
            made.push_back(cloneAsArray(original, step, copies));
            continue;
        }
        for (int k = 1; k <= copies; ++k) made.push_back(placeClone(original, step, k, kind == CloneKind::Instance && cloneAllows(kind)));
    }
    cloneOf_.clear();
    refreshList();
    setSelection(made, made.empty() ? 0 : made.back());
    applyEdit(0);
    const QString what = kind == CloneKind::Array      ? ruPlural(int(made.size()), "массив", "массива", "массивов")
                         : kind == CloneKind::Instance ? ruPlural(int(made.size()), "экземпляр", "экземпляра", "экземпляров")
                                                       : ruPlural(int(made.size()), "копия", "копии", "копий");
    emit statusMessage("Клонировано: " + what + " · Ctrl+Z — отменить всё сразу");
}

// Отмена, Esc or a click past the popover: the scene as before the drag, and no undo step for it.
void SceneBuilder::cancelClone() {
    if (!clonePending_) return;
    clonePending_ = false;
    graph_ = gizmoGraph_;
    if (!undo_.empty()) undo_.pop_back();
    updateHistoryActions();
    std::vector<uint32_t> originals;
    for (const auto& [original, preview] : cloneOf_) originals.push_back(original);
    cloneOf_.clear();
    refreshList();
    setSelection(originals, originals.empty() ? 0 : originals.front());
    applyEdit(0);
}
