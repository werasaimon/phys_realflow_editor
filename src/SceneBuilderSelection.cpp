// What is selected in the scene builder (see SceneBuilder.h), and what works on all of it: one object
// is active - the inspector and the gizmo show it - and Shift / Ctrl + click or the selection box add
// more (docs/controls.md: Shift and Ctrl both toggle, so any habit works). An object is a shape, a
// group or an array alike. Delete, duplicate, hide and the gizmo act on every selected object - a
// member of a selected group goes with its group -; a gizmo drag is one undo step and Esc puts them
// back. The gizmo works in the world: an object in a group is moved where the mouse takes it, its
// pose written back relative to the group.
#include "SceneBuilder.h"

#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>

using namespace rf;

bool SceneBuilder::isSelected(uint32_t id) const {
    return id != 0 && std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void SceneBuilder::setSelected(uint32_t id) { setSelection(id ? std::vector<uint32_t>{id} : std::vector<uint32_t>{}, id); }

// The one place the selection changes: unknown ids and repeats are dropped, the active one is kept
// if it is still selected, else the last one added becomes active.
void SceneBuilder::setSelection(std::vector<uint32_t> ids, uint32_t active) {
    std::vector<uint32_t> kept;
    for (uint32_t id : ids)
        if (findObject(graph_, id) && std::find(kept.begin(), kept.end(), id) == kept.end()) kept.push_back(id);
    selection_ = std::move(kept);
    selectedId_ = isSelected(active) ? active : (selection_.empty() ? 0 : selection_.back());
    syncListSelection();
    fillInspector();
    sendViewportFlags();
    updateGizmoTarget();
    refreshColliderGuides();
    refreshMarkers();
    emit selectionChanged(selectedId_);
}

void SceneBuilder::syncListSelection() {
    const QSignalBlocker quiet(list_);
    for (QTreeWidgetItemIterator it(list_); *it; ++it) {
        QTreeWidgetItem* item = *it;
        const uint32_t id = item->data(0, Qt::UserRole).toUInt();
        item->setSelected(isSelected(id));
        if (id == selectedId_) list_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
    }
}

// Shift / Ctrl + click: a thing inside a selected group takes the group away; else it is added.
void SceneBuilder::toggleSelected(uint32_t id) {
    const SceneObject* o = findObject(graph_, id);
    if (!o || o->locked) return;
    std::vector<uint32_t> ids = selection_;
    for (uint32_t up : chainOf(id)) {
        if (!isSelected(up)) continue;
        ids.erase(std::remove(ids.begin(), ids.end(), up), ids.end());
        return setSelection(ids, selectedId_ == up ? 0 : selectedId_);
    }
    ids.push_back(id);
    setSelection(ids, id);
}

void SceneBuilder::selectAll() { setSelection(topLevelObjects(), selectedId_); }

// The box: every visible, unlocked thing whose outline on screen (the corners of its bounds,
// projected) touches the rectangle selects its topmost group (or its array). Shift adds to the
// selection, Ctrl takes away (Blender's rule).
void SceneBuilder::selectInRect(const QRectF& rect, const GizmoView& view, bool add, bool remove) {
    std::vector<uint32_t> ids = add || remove ? selection_ : std::vector<uint32_t>{};
    for (const Entity& w : worldEntities(graph_)) {
        if (!w.visible || w.locked) continue;
        const std::vector<uint32_t> chain = chainOf(ownerOf(w.id));
        if (chain.empty()) continue;
        const AABB box = editView_.worldBounds(graph_, w);
        QRectF onScreen;
        bool behind = false;
        for (int c = 0; c < 8; ++c) {
            const Vector3 p((c & 1) ? box.hi.x : box.lo.x, (c & 2) ? box.hi.y : box.lo.y, (c & 4) ? box.hi.z : box.lo.z);
            if (dot(p - view.eye(), view.forward()) <= 0.0f) behind = true;
            const Vector2 s = view.project(p);
            onScreen = c == 0 ? QRectF(s.x, s.y, 0, 0) : onScreen.united(QRectF(s.x, s.y, 0, 0));
        }
        if (behind || !onScreen.adjusted(-1, -1, 1, 1).intersects(rect)) continue;
        const uint32_t target = chain.back();
        if (remove) ids.erase(std::remove(ids.begin(), ids.end(), target), ids.end());
        else if (std::find(ids.begin(), ids.end(), target) == ids.end()) ids.push_back(target);
    }
    setSelection(ids, selectedId_);
}

void SceneBuilder::removeSelected() {
    const std::vector<uint32_t> gone = selectionRoots();
    if (gone.empty()) return;
    prepareEdit(0);
    remember();
    removeObjects(gone);
    selection_.clear();
    refreshList();
    setSelection({}, 0);
    applyEdit(0);
}

// Copies in place (Ctrl+D), a little above the originals; the copies become the selection.
void SceneBuilder::duplicateSelected() {
    const std::vector<uint32_t> roots = selectionRoots();
    if (roots.empty()) return;
    prepareEdit(0);
    remember();
    std::vector<uint32_t> copies;
    for (uint32_t id : roots) {
        const AABB box = boundsOf(id);
        const uint32_t c = copySubtree(id, false, objectById(id)->parent);
        SceneObject* o = objectById(c);
        o->name += " копия";
        Vector3 p;
        Quaternion q;
        worldPose(graph_, *o, p, q);
        const Vector3 keep = o->rotationDeg;
        setWorldPose(graph_, *o, p + Vector3(0.0f, (box.valid() ? box.extent().y : 0.2f) + 0.02f, 0.0f), q); // on top of the original
        o->rotationDeg = keep;
        copies.push_back(c);
    }
    refreshList();
    setSelection(copies, copies.back());
    applyEdit(0);
    frameObjects();
}

void SceneBuilder::hideSelected() {
    const std::vector<uint32_t> roots = selectionRoots();
    if (roots.empty()) return;
    prepareEdit(0);
    remember();
    for (uint32_t id : roots) objectById(id)->visible = false;
    refreshList();
    setSelection({}, 0);
    applyEdit(0);
}

// Everything is shown again, except the hidden templates of arrays (the arrays draw them).
void SceneBuilder::unhideAll() {
    prepareEdit(0);
    remember();
    for (Entity& e : graph_.entities)
        if (!isHiddenTemplate(e)) e.visible = true;
    for (Group& g : graph_.groups) g.visible = true;
    for (ArrayObject& a : graph_.arrays) a.visible = true;
    for (Light& l : graph_.lights) l.visible = true;
    for (Camera& c : graph_.cameras) c.visible = true;
    refreshList();
    fillInspector();
    applyEdit(0);
}

// ---------------------------------------------------------------------------
// The gizmo: a drag is one undo step; Esc puts the objects back as they were
// ---------------------------------------------------------------------------
void SceneBuilder::onGizmoStarted() {
    if (clonePending_) cancelClone(); // a popover left open: its clone is dropped
    SceneObject* active = selectedObject();
    gizmoEntity_ = 0;
    gizmoStarts_.clear();
    cloneDrag_ = false;
    if (!active || !gizmoAllowed()) return;
    remember();
    if (!editing() && liveTargetValid_ && entityById(active->id)) { // on pause: from where it is now
        Vector3 p;
        Quaternion q;
        worldPose(graph_, *active, p, q);
        const Vector3 keep = active->rotationDeg;
        setWorldPose(graph_, *active, liveTarget_, q);
        active->rotationDeg = keep;
    }
    gizmoGraph_ = graph_;
    lastPose_ = GizmoPose();
    for (uint32_t id : selectionRoots()) {
        const SceneObject* o = findObject(graph_, id);
        if (!effectivelyVisible(graph_, *o)) continue;
        GizmoStart s;
        worldPose(graph_, *o, s.position, s.rotation);
        gizmoStarts_[id] = s;
    }
    if (gizmoStarts_.empty()) return;
    gizmoEntity_ = active->id;
    worldPose(graph_, *active, gizmoFrom_.position, gizmoFrom_.rotation);
}

namespace {
const Entity* entityIn(const SceneGraph& g, uint32_t id) {
    for (const Entity& e : g.entities)
        if (e.id == id) return &e;
    return nullptr;
}
} // namespace

// A scale: every shape inside the object (for a shape: the geometry it shows, an instance's master)
// gets its size from `start` times the factors, and inside a group or an array the distances between
// the things grow by the same factors. `start` may be the graph itself (then it scales in place).
void SceneBuilder::scaleFrom(const SceneGraph& start, uint32_t id, const Vector3& f) {
    if (ArrayObject* a = arrayById(id)) {
        for (const ArrayObject& s : start.arrays)
            if (s.id == id) a->step = s.step * f, a->radius = s.radius * std::max(f.x, f.z);
        return;
    }
    std::vector<uint32_t> sized; // a master shared by several instances is scaled once
    for (const Entity& e : graph_.entities) {
        if (!isInside(e.id, id)) continue;
        Entity* geometry = entityById(masterOf(e.id));
        const Entity* s = entityIn(start, geometry->id);
        if (!s || std::find(sized.begin(), sized.end(), geometry->id) != sized.end()) continue;
        geometry->size = vmax(s->size * f, Vector3(0.005f));
        sized.push_back(geometry->id);
    }
    if (!groupById(id)) return;
    for (uint32_t kid : childrenOf(id)) // the members' places in the group
        if (const SceneObject* s = findObject(start, kid)) objectById(kid)->position = s->position * f;
}

// Every dragged object follows the gizmo in the world: moved by the same step, turned about the
// gizmo's centre by the same turn, scaled about its own centre by the same factors.
void SceneBuilder::onGizmoMoved(const GizmoPose& pose) {
    if (!gizmoEntity_) return;
    lastPose_ = pose;
    const Quaternion turn = (pose.rotation * gizmoFrom_.rotation.conjugate()).normalized();
    for (const auto& [id, start] : gizmoStarts_) {
        SceneObject* o = objectById(id);
        if (!o) continue;
        const Vector3 keep = o->rotationDeg;
        switch (pose.mode) {
        case GizmoMode::Translate:
            setWorldPose(graph_, *o, start.position + (pose.position - gizmoFrom_.position), start.rotation);
            o->rotationDeg = keep; // a move never turns: the angles stay exactly as typed
            break;
        case GizmoMode::Rotate:
            setWorldPose(graph_, *o, gizmoFrom_.position + turn.rotate(start.position - gizmoFrom_.position), (turn * start.rotation).normalized());
            break;
        case GizmoMode::Scale: scaleFrom(gizmoGraph_, id, pose.scale); break;
        case GizmoMode::Select: break;
        }
    }
    if (editing()) refreshView(); // on pause the simulation takes the new pose on release
    else updateGizmoTarget(), refreshLightsAndCameras();
    if (const SceneObject* active = selectedObject()) showTransform(*active);
}

void SceneBuilder::onGizmoFinished() {
    const bool moved = gizmoEntity_ != 0;
    gizmoEntity_ = 0;
    gizmoStarts_.clear();
    if (moved && cloneDrag_) { // the copy stands where it was dragged: the popover asks how many (MainWindow)
        cloneDrag_ = false;
        clonePending_ = true;
        return;
    }
    fillInspector();
    if (!editing() && moved) applyEdit(0);
}

void SceneBuilder::onGizmoCancelled() {
    if (!gizmoEntity_) return;
    graph_ = gizmoGraph_;
    gizmoEntity_ = 0;
    gizmoStarts_.clear();
    if (!undo_.empty()) undo_.pop_back(); // nothing happened: no undo step either
    updateHistoryActions();
    if (cloneDrag_) { // the moving copies go; the originals are the selection again
        cloneDrag_ = false;
        std::vector<uint32_t> originals;
        for (const auto& [original, copy] : cloneOf_) originals.push_back(original);
        cloneOf_.clear();
        refreshList();
        setSelection(originals, originals.empty() ? 0 : originals.front());
    }
    fillInspector();
    if (editing()) refreshView();
    else updateGizmoTarget();
}
