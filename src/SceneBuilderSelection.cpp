// What is selected in the scene builder (see SceneBuilder.h), and what works on all of it: one object
// is active - the inspector and the gizmo show it - and Shift / Ctrl + click or the selection box add
// more (docs/controls.md: Shift and Ctrl both toggle, so any habit works). Delete, duplicate, hide
// and the gizmo act on every selected object; a gizmo drag is one undo step and Esc puts them back.
#include "SceneBuilder.h"

#include <QSignalBlocker>
#include <QTreeWidget>

#include <algorithm>

using namespace rf;

bool SceneBuilder::isSelected(uint32_t id) const {
    return id != 0 && std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void SceneBuilder::setSelected(uint32_t id) { setSelection(id ? std::vector<uint32_t>{id} : std::vector<uint32_t>{}, id); }

// The one place the selection changes: unknown ids are dropped, the active one is kept if it is still
// selected, else the last one added becomes active.
void SceneBuilder::setSelection(std::vector<uint32_t> ids, uint32_t active) {
    ids.erase(std::remove_if(ids.begin(), ids.end(), [this](uint32_t id) { return indexOf(id) < 0; }), ids.end());
    selection_ = std::move(ids);
    selectedId_ = isSelected(active) ? active : (selection_.empty() ? 0 : selection_.back());
    syncListSelection();
    fillInspector();
    sendViewportFlags();
    updateGizmoTarget();
    refreshColliderGuides();
    emit selectionChanged(selectedId_);
}

void SceneBuilder::syncListSelection() {
    const QSignalBlocker quiet(list_);
    for (int r = 0; r < list_->topLevelItemCount(); ++r) {
        QTreeWidgetItem* item = list_->topLevelItem(r);
        const uint32_t id = item->data(0, Qt::UserRole).toUInt();
        item->setSelected(isSelected(id));
        if (id == selectedId_) list_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
    }
}

void SceneBuilder::toggleSelected(uint32_t id) {
    const int i = indexOf(id);
    if (i < 0 || graph_.entities[size_t(i)].locked) return;
    std::vector<uint32_t> ids = selection_;
    if (isSelected(id)) {
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        setSelection(ids, selectedId_ == id ? 0 : selectedId_);
    } else {
        ids.push_back(id);
        setSelection(ids, id);
    }
}

void SceneBuilder::selectAll() {
    std::vector<uint32_t> ids;
    for (const Entity& e : graph_.entities)
        if (e.visible && !e.locked) ids.push_back(e.id);
    setSelection(ids, selectedId_);
}

// The box: every visible, unlocked object whose outline on screen (the corners of its bounds,
// projected) touches the rectangle. Shift adds to the selection, Ctrl takes away (Blender's rule).
void SceneBuilder::selectInRect(const QRectF& rect, const GizmoView& view, bool add, bool remove) {
    std::vector<uint32_t> ids = add || remove ? selection_ : std::vector<uint32_t>{};
    for (const Entity& e : graph_.entities) {
        if (!e.visible || e.locked) continue;
        const AABB box = editView_.worldBounds(graph_, e);
        QRectF onScreen;
        bool behind = false;
        for (int c = 0; c < 8; ++c) {
            const Vector3 p((c & 1) ? box.hi.x : box.lo.x, (c & 2) ? box.hi.y : box.lo.y, (c & 4) ? box.hi.z : box.lo.z);
            if (dot(p - view.eye(), view.forward()) <= 0.0f) behind = true;
            const Vector2 s = view.project(p);
            onScreen = c == 0 ? QRectF(s.x, s.y, 0, 0) : onScreen.united(QRectF(s.x, s.y, 0, 0));
        }
        if (behind || !onScreen.adjusted(-1, -1, 1, 1).intersects(rect)) continue;
        if (remove) ids.erase(std::remove(ids.begin(), ids.end(), e.id), ids.end());
        else if (std::find(ids.begin(), ids.end(), e.id) == ids.end()) ids.push_back(e.id);
    }
    setSelection(ids, selectedId_);
}

void SceneBuilder::removeSelected() {
    if (selection_.empty()) return;
    prepareEdit(0);
    remember();
    const std::vector<uint32_t> gone = selection_;
    graph_.entities.erase(std::remove_if(graph_.entities.begin(), graph_.entities.end(),
                                         [&](const Entity& e) { return !e.locked && std::count(gone.begin(), gone.end(), e.id); }),
                          graph_.entities.end());
    selection_.clear();
    refreshList();
    setSelection({}, 0);
    applyEdit(0);
}

// Copies in place (Ctrl+D), a little above the originals; the copies become the selection.
void SceneBuilder::duplicateSelected() {
    if (selection_.empty()) return;
    prepareEdit(0);
    remember();
    std::vector<uint32_t> copies;
    for (uint32_t id : selection_) {
        Entity e = graph_.entities[size_t(indexOf(id))];
        e.id = newId();
        e.name += " копия";
        e.position.y += e.size.y + 0.02f; // on top of the original
        e.locked = false;
        graph_.entities.push_back(e);
        copies.push_back(e.id);
    }
    refreshList();
    setSelection(copies, copies.back());
    applyEdit(0);
    frameObjects();
}

void SceneBuilder::hideSelected() {
    if (selection_.empty()) return;
    prepareEdit(0);
    remember();
    for (uint32_t id : selection_) graph_.entities[size_t(indexOf(id))].visible = false;
    refreshList();
    setSelection({}, 0);
    applyEdit(0);
}

void SceneBuilder::unhideAll() {
    prepareEdit(0);
    remember();
    for (Entity& e : graph_.entities) e.visible = true;
    refreshList();
    fillInspector();
    applyEdit(0);
}

// ---------------------------------------------------------------------------
// The gizmo: a drag is one undo step; Esc puts the objects back as they were
// ---------------------------------------------------------------------------
void SceneBuilder::onGizmoStarted() {
    Entity* e = selectedEntity();
    gizmoEntity_ = 0;
    gizmoStarts_.clear();
    if (!e || !gizmoAllowed()) return;
    remember();
    if (!editing() && liveTargetValid_) e->position = liveTarget_; // on pause: from where it is now
    gizmoEntity_ = e->id;
    for (uint32_t id : selection_) {
        const Entity& x = graph_.entities[size_t(indexOf(id))];
        if (!x.locked && x.visible) gizmoStarts_[id] = x;
    }
}

// The active object follows the gizmo; the others move by the same step, turn around the active
// one's centre by the same turn, and scale about their own centres by the same factors.
void SceneBuilder::onGizmoMoved(const GizmoPose& pose) {
    const auto active = gizmoStarts_.find(gizmoEntity_);
    if (active == gizmoStarts_.end()) return;
    const Entity& a = active->second;
    const Quaternion turn = pose.rotation * quaternionFromEulerDeg(a.rotationDeg).conjugate();
    for (const auto& [id, start] : gizmoStarts_) {
        Entity& e = graph_.entities[size_t(indexOf(id))];
        switch (pose.mode) {
        case GizmoMode::Translate: e.position = start.position + (pose.position - a.position); break;
        case GizmoMode::Rotate:
            e.rotationDeg = eulerDegFromQuaternion(turn * quaternionFromEulerDeg(start.rotationDeg));
            e.position = a.position + turn.rotate(start.position - a.position);
            break;
        case GizmoMode::Scale: e.size = vmax(start.size * pose.scale, Vector3(0.005f)); break;
        case GizmoMode::Select: break;
        }
    }
    Entity& e = graph_.entities[size_t(indexOf(gizmoEntity_))];
    if (editing()) refreshView(); // on pause the simulation takes the new pose on release
    else updateGizmoTarget();
    showTransform(e);
}

void SceneBuilder::onGizmoFinished() {
    const bool moved = gizmoEntity_ != 0;
    gizmoEntity_ = 0;
    gizmoStarts_.clear();
    fillInspector();
    if (!editing() && moved) applyEdit(0);
}

void SceneBuilder::onGizmoCancelled() {
    if (!gizmoEntity_) return;
    for (const auto& [id, start] : gizmoStarts_) graph_.entities[size_t(indexOf(id))] = start;
    gizmoEntity_ = 0;
    gizmoStarts_.clear();
    if (!undo_.empty()) undo_.pop_back(); // nothing happened: no undo step either
    updateHistoryActions();
    fillInspector();
    if (editing()) refreshView();
    else updateGizmoTarget();
}
