// The scene builder's logic (see SceneBuilder.h): creating and removing things, turning roles on
// and off together with what they need, the gizmo's edits, the undo history, the edit / play / stop
// modes and what the viewport shows in each. The panels are built in SceneBuilderPanels.cpp.
#include "SceneBuilder.h"

#include "InspectorWidgets.h"
#include "ObjectInspector.h"
#include "RoleBar.h"

#include <QAction>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>

using namespace rf;

namespace {

// Colours for new shapes, in turn, so neighbours differ at a glance.
const Vector3 kPalette[] = {{0.91f, 0.50f, 0.28f}, {0.31f, 0.59f, 0.92f}, {0.43f, 0.77f, 0.38f}, {0.93f, 0.77f, 0.31f},
                            {0.72f, 0.47f, 0.88f}, {0.33f, 0.80f, 0.80f}, {0.93f, 0.42f, 0.55f}};

void setRole(Entity& e, RoleIcon role, bool on) {
    switch (role) {
    case RoleIcon::Rigid: e.rigid.enabled = on; break;
    case RoleIcon::Soft: e.soft.enabled = on; break;
    case RoleIcon::Liquid: e.liquid.enabled = on; break;
    case RoleIcon::Cloth: e.cloth.enabled = on; break;
    case RoleIcon::Magnet: e.magnet.enabled = on; break;
    case RoleIcon::Smoke: e.emitter.enabled = on; break;
    case RoleIcon::Flame: e.flammable.enabled = on; break;
    case RoleIcon::Heat: e.heat.enabled = on; break;
    case RoleIcon::Count: break;
    }
}

bool madeOfSomething(const Entity& e) { return e.rigid.enabled || e.soft.enabled || e.liquid.enabled || e.cloth.enabled; }

} // namespace

const char* shapeName(ShapeKind k) {
    static const char* names[] = {"Куб", "Сфера", "Цилиндр", "Конус", "Плоскость", "Модель"};
    return names[std::clamp(int(k), 0, 5)];
}

SceneBuilder::SceneBuilder(SimController* ctrl, QObject* parent) : QObject(parent), ctrl_(ctrl) {
    lastRemember_.start();
    buildActions();
    buildSceneList();
    buildInspector();
    fillWorld();
    refreshList();
    fillInspector();
    updateHistoryActions();
}

// ---------------------------------------------------------------------------
// Every edit starts here
// ---------------------------------------------------------------------------
// A ready-made scene of the SDK cannot be edited: an edit takes the builder's own scene back.
void SceneBuilder::prepareEdit(uint32_t entityId) {
    (void)entityId;
    if (sample_) returnToEdit(true);
}

void SceneBuilder::applyEdit(uint32_t entityId) {
    if (editing()) refreshView();
    else applyEditDuringPlay(entityId);
}

namespace {
std::string entityText(const Entity& e) {
    SceneGraph one;
    one.entities.push_back(e);
    return one.save();
}
std::string worldText(const SceneGraph& g) {
    SceneGraph world;
    world.world = g.world;
    return world.save();
}
bool samePose(const Entity& a, const Entity& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.rotationDeg.x == b.rotationDeg.x && a.rotationDeg.y == b.rotationDeg.y && a.rotationDeg.z == b.rotationDeg.z;
}
} // namespace

// An edit while the simulation runs: the objects that differ from what the simulation was last
// given are rebuilt in place between frames (added, removed), the rest runs on untouched. An object
// the editor did not move keeps its live pose and velocity (the simulation's own pose is sent back,
// so the SDK sees it unmoved). The world itself needs a reload: the builder stops and says why.
void SceneBuilder::applyEditDuringPlay(uint32_t entityId) {
    (void)entityId; // every changed object is found by comparing, whichever edit made it
    if (worldText(graph_) != worldText(simGraph_))
        return reloadNeeded("Тяжесть, коробку и газ меняют только перезапуском сцены");
    std::vector<std::pair<Entity, bool>> changed; // the entity and whether the editor moved it
    std::vector<uint32_t> removed;
    for (const Entity& e : graph_.entities) {
        const Entity* before = nullptr;
        for (const Entity& s : simGraph_.entities)
            if (s.id == e.id) before = &s;
        if (!before || entityText(*before) != entityText(e)) changed.push_back({e, !before || !samePose(*before, e)});
    }
    for (const Entity& s : simGraph_.entities)
        if (indexOf(s.id) < 0) removed.push_back(s.id);
    simGraph_ = graph_;
    refreshScenery(); // a shape that lost (or has no) role is still drawn
    if (changed.empty() && removed.empty()) return;
    QPointer<SceneBuilder> self(this);
    ctrl_->post([changed, removed, self](Simulation& s) {
        auto* scene = dynamic_cast<GraphScene*>(s.scene());
        if (!scene) return;
        bool ok = true;
        for (uint32_t id : removed) scene->removeEntity(s, id);
        std::vector<std::pair<uint32_t, std::pair<Vector3, Vector3>>> poses;
        for (const auto& [entity, moved] : changed) {
            Entity e = entity;
            for (const Entity& held : scene->graph().entities) // unmoved: the pose the scene holds, so it keeps the live one
                if (held.id == e.id && !moved) e.position = held.position, e.rotationDeg = held.rotationDeg;
            ok = scene->rebuildEntity(s, e) && ok;
            for (const Entity& held : scene->graph().entities)
                if (held.id == e.id) poses.push_back({e.id, {held.position, held.rotationDeg}});
        }
        std::vector<uint32_t> bodyEntity(s.rigid.bodies().size(), 0);
        for (size_t b = 0; b < bodyEntity.size(); ++b) bodyEntity[b] = scene->entityIdOfBody(int(b));
        QMetaObject::invokeMethod(self, [self, ok, bodyEntity, poses] {
            if (self) self->onPlayEditApplied(ok, bodyEntity, poses);
        }, Qt::QueuedConnection);
    });
}

// Back from the simulation: the poses it now holds become the builder's too (so the next edit does
// not look like a move), and which body belongs to whom.
void SceneBuilder::onPlayEditApplied(bool ok, const std::vector<uint32_t>& bodyEntity,
                                     const std::vector<std::pair<uint32_t, std::pair<Vector3, Vector3>>>& poses) {
    if (editing()) return;
    for (const auto& [id, pose] : poses)
        for (SceneGraph* g : {&graph_, &simGraph_})
            for (Entity& e : g->entities)
                if (e.id == id) e.position = pose.first, e.rotationDeg = pose.second;
    bodyEntity_ = bodyEntity;
    sendViewportFlags();
    if (const Entity* e = selectedEntity()) showTransform(*e);
    if (!ok) reloadNeeded("Включение газа требует перезапуска сцены");
}

// A change the running simulation cannot take: stop with the edit kept, and say what to do.
void SceneBuilder::reloadNeeded(const QString& why) {
    returnToEdit(true);
    emit statusMessage(why + ". Правка сохранена — нажмите ▶ Пуск.");
}

// ---------------------------------------------------------------------------
// Creating and removing: a new shape is geometry only, standing on the floor
// ---------------------------------------------------------------------------
Vector3 SceneBuilder::freeSpot() const {
    Vector3 p(0.0f);
    for (int k = 0; k < 25; ++k) { // places on a 5 x 5 grid around the centre
        p = Vector3(0.45f * float(k % 5 - 2), 0.0f, 0.45f * float(k / 5 - 2));
        const bool taken = std::any_of(graph_.entities.begin(), graph_.entities.end(), [&](const Entity& o) {
            return o.shape != ShapeKind::Plane && std::hypot(o.position.x - p.x, o.position.z - p.z) < 0.3f;
        });
        if (!taken) break;
    }
    return p;
}

void SceneBuilder::addEntity(ShapeKind shape) {
    prepareEdit(0);
    remember();
    Entity e;
    e.id = newId();
    e.shape = shape;
    int same = 0;
    for (const Entity& o : graph_.entities) same += o.shape == shape;
    e.name = QString("%1 %2").arg(shapeName(shape)).arg(same + 1).toStdString();
    e.color = kPalette[graph_.entities.size() % 7];
    if (shape == ShapeKind::Plane) {
        e.size = Vector3(3.0f, 0.02f, 3.0f);
        e.position = Vector3(0.0f, 0.03f, 0.0f); // just above the floor
        e.color = Vector3(0.62f, 0.64f, 0.68f);
    } else {
        e.position = freeSpot();
        e.position.y = 0.5f * e.size.y; // standing on the floor
    }
    graph_.entities.push_back(e);
    setSelected(e.id);
    refreshList();
    applyEdit(e.id);
    frameObjects();
}

bool SceneBuilder::importModel(const QString& path, QString& error) {
    prepareEdit(0);
    Entity e;
    e.shape = ShapeKind::Mesh;
    e.meshFile = path.toStdString();
    e.size = Vector3(1.0f);
    std::string err;
    const TriMesh m = entityLocalMesh(e, graph_.baseDirectory, &err);
    if (m.triangles.empty()) {
        error = err.empty() ? QString("в файле нет треугольников") : QString::fromStdString(err);
        return false;
    }
    const Vector3 extent = m.bounds().extent();
    e.size = extent / std::max(maxComp(extent), 1e-6f); // its largest side 1 m, proportions kept
    remember();
    e.id = newId();
    e.name = QFileInfo(path).completeBaseName().toStdString();
    e.color = kPalette[graph_.entities.size() % 7];
    e.position = freeSpot();
    e.position.y = 0.5f * e.size.y;
    graph_.entities.push_back(e);
    setSelected(e.id);
    refreshList();
    applyEdit(e.id);
    frameObjects();
    return true;
}

void SceneBuilder::removeSelected() {
    const int i = indexOf(selectedId_);
    if (i < 0) return;
    prepareEdit(selectedId_);
    remember();
    graph_.entities.erase(graph_.entities.begin() + i);
    const int next = std::min(i, int(graph_.entities.size()) - 1);
    selectedId_ = next >= 0 && !graph_.entities[size_t(next)].locked ? graph_.entities[size_t(next)].id : 0;
    refreshList();
    fillInspector();
    applyEdit(0);
}

void SceneBuilder::duplicateSelected() {
    if (!selectedEntity()) return;
    prepareEdit(selectedId_);
    remember();
    Entity e = *selectedEntity();
    e.id = newId();
    e.name += " копия";
    e.position.y += e.size.y + 0.02f; // on top of the original
    e.locked = false;
    graph_.entities.push_back(e);
    setSelected(e.id);
    refreshList();
    applyEdit(e.id);
    frameObjects();
}

void SceneBuilder::toggleVisible(uint32_t id) {
    editEntity(id, [](Entity& e) { e.visible = !e.visible; });
}

void SceneBuilder::toggleLocked(uint32_t id) {
    editEntity(id, [](Entity& e) { e.locked = !e.locked; });
    if (const Entity* e = selectedEntity(); e && e->locked) setSelected(0); // a locked thing is not edited
}

void SceneBuilder::frameSelected() {
    const Entity* e = selectedEntity();
    if (!e) return frameObjects();
    AABB box = editView_.worldBounds(graph_, *e);
    const Vector3 margin(std::max(0.15f, 0.4f * maxComp(box.extent())));
    emit frameRequested(AABB(box.lo - margin, box.hi + margin));
}

void SceneBuilder::editEntity(uint32_t id, const std::function<void(Entity&)>& change) {
    if (indexOf(id) < 0) return;
    prepareEdit(id);
    remember();
    change(graph_.entities[size_t(indexOf(id))]);
    refreshList();
    if (id == selectedId_) fillInspector();
    applyEdit(id);
}

// ---------------------------------------------------------------------------
// Roles: one click turns the shape into a model; the role switches on what it needs
// ---------------------------------------------------------------------------
void SceneBuilder::toggleRole(RoleIcon role) {
    if (!selectedEntity()) {
        emit statusMessage("Сначала выберите объект — или создайте его кнопкой сверху");
        return;
    }
    prepareEdit(selectedId_);
    Entity* e = selectedEntity();
    remember();
    const bool on = !roleEnabled(*e, role);
    if (on && isMadeOfRole(role)) // made of one thing at a time
        for (RoleIcon other : {RoleIcon::Rigid, RoleIcon::Soft, RoleIcon::Liquid, RoleIcon::Cloth}) setRole(*e, other, false);
    setRole(*e, role, on);
    if (on) switchOnWhatRoleNeeds(*e, role);
    fillInspector();
    refreshList();
    applyEdit(selectedId_);
}

// The magic, only where it cannot surprise: a role switches on what IT needs to work.
void SceneBuilder::switchOnWhatRoleNeeds(Entity& e, RoleIcon role) {
    auto needGas = [this](const QString& why) {
        if (graph_.world.gas) return;
        graph_.world.gas = true;
        fillWorld();
        emit statusMessage("Включён газ: " + why);
    };
    switch (role) {
    case RoleIcon::Cloth:
        if (e.cloth.pinnedEdges == 0) e.cloth.pinnedEdges = 16; // hangs from a rod, like a curtain
        if (e.shape == ShapeKind::Plane && e.position.y < 0.6f) e.position.y = 1.2f; // off the floor to hang
        emit statusMessage("Ткань будет висеть на пруте; края закрепляются в разделе «Ткань»");
        break;
    case RoleIcon::Magnet:
        if (!madeOfSomething(e)) { // a magnet needs a body to push
            e.rigid.enabled = true;
            e.rigid.fixed = false;
        }
        if (e.magnet.moment.x == 0 && e.magnet.moment.y == 0 && e.magnet.moment.z == 0) e.magnet.moment = Vector3(0, 1, 0);
        break;
    case RoleIcon::Smoke:
        if (e.emitter.smoke <= 0 && e.emitter.liquid <= 0) e.emitter.smoke = 1;
        needGas("дым нуждается в воздухе");
        break;
    case RoleIcon::Flame: needGas("огню нужен воздух с кислородом"); break;
    case RoleIcon::Heat: needGas("тепло поднимает воздух"); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Edits from the widgets
// ---------------------------------------------------------------------------
void SceneBuilder::onObjectEdited() {
    if (filling_ || !selectedEntity()) return;
    prepareEdit(selectedId_);
    remember(true);
    object_->writeTo(*selectedEntity());
    refreshList();
    refreshHeader();
    applyEdit(selectedId_);
}

void SceneBuilder::onDetailsEdited() {
    if (filling_ || !selectedEntity()) return;
    prepareEdit(selectedId_);
    remember(true);
    readDetails(*selectedEntity());
    refreshList();
    refreshHeader();
    applyEdit(selectedId_);
}

void SceneBuilder::onWorldEdited() {
    if (filling_) return;
    prepareEdit(0);
    remember(true);
    graph_.world.gravity = gravity_->value();
    graph_.world.size = worldSize_->value();
    graph_.world.gas = gas_->isChecked();
    graph_.world.magneticGas = plasma_->isChecked();
    applyEdit(0);
}

// The eye and the lock are clicked right in the list; any other column selects the row.
void SceneBuilder::onListClicked(QTreeWidgetItem* item, int column) {
    const uint32_t id = item->data(0, Qt::UserRole).toUInt();
    if (indexOf(id) < 0) return;
    if (column == 1 || column == 2) {
        prepareEdit(id);
        remember();
        Entity& e = graph_.entities[size_t(indexOf(id))];
        if (column == 1) e.visible = !e.visible;
        else e.locked = !e.locked;
        refreshList();
        fillInspector();
        applyEdit(id);
        return;
    }
    setSelected(id);
}

void SceneBuilder::setSelected(uint32_t id) {
    selectedId_ = indexOf(id) >= 0 ? id : 0;
    {
        const QSignalBlocker quiet(list_);
        list_->clearSelection();
        for (int r = 0; r < list_->topLevelItemCount(); ++r)
            if (list_->topLevelItem(r)->data(0, Qt::UserRole).toUInt() == selectedId_) list_->setCurrentItem(list_->topLevelItem(r));
    }
    fillInspector();
    sendViewportFlags();
    updateGizmoTarget();
}

// ---------------------------------------------------------------------------
// The gizmo: a drag is one undo step; Esc puts the object back as it was
// ---------------------------------------------------------------------------
void SceneBuilder::onGizmoStarted() {
    Entity* e = selectedEntity();
    gizmoEntity_ = 0;
    if (!e || !gizmoAllowed()) return;
    remember();
    if (!editing() && liveTargetValid_) e->position = liveTarget_; // on pause: from where it is now
    gizmoEntity_ = e->id;
    gizmoStart_ = *e;
}

void SceneBuilder::onGizmoMoved(const GizmoPose& pose) {
    const int i = indexOf(gizmoEntity_);
    if (i < 0) return;
    Entity& e = graph_.entities[size_t(i)];
    switch (pose.mode) {
    case GizmoMode::Translate: e.position = pose.position; break;
    case GizmoMode::Rotate: e.rotationDeg = eulerDegFromQuaternion(pose.rotation); break;
    case GizmoMode::Scale: e.size = vmax(gizmoStart_.size * pose.scale, Vector3(0.005f)); break;
    case GizmoMode::Select: break;
    }
    if (editing()) refreshView(); // on pause the simulation takes the new pose on release
    else updateGizmoTarget();
    showTransform(e);
}

void SceneBuilder::onGizmoFinished() {
    const uint32_t id = gizmoEntity_;
    gizmoEntity_ = 0;
    fillInspector();
    if (!editing() && id) applyEdit(id);
}

void SceneBuilder::onGizmoCancelled() {
    const int i = indexOf(gizmoEntity_);
    gizmoEntity_ = 0;
    if (i < 0) return;
    graph_.entities[size_t(i)] = gizmoStart_;
    if (!undo_.empty()) undo_.pop_back(); // nothing happened: no undo step either
    updateHistoryActions();
    fillInspector();
    if (editing()) refreshView();
    else updateGizmoTarget();
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------
// Called before a change. Typing or dragging a number makes many small changes within a moment:
// those merge into one step, so one Ctrl+Z undoes the whole drag.
void SceneBuilder::remember(bool mergeWithLast) {
    const bool recent = lastRemember_.elapsed() < 700;
    lastRemember_.restart();
    if (mergeWithLast && recent && !undo_.empty()) return;
    undo_.push_back(graph_);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
    updateHistoryActions();
}

void SceneBuilder::undo() {
    if (undo_.empty()) return;
    prepareEdit(0);
    redo_.push_back(graph_);
    SceneGraph g = std::move(undo_.back());
    undo_.pop_back();
    restore(std::move(g));
}

void SceneBuilder::redo() {
    if (redo_.empty()) return;
    prepareEdit(0);
    undo_.push_back(graph_);
    SceneGraph g = std::move(redo_.back());
    redo_.pop_back();
    restore(std::move(g));
}

void SceneBuilder::restore(SceneGraph g) {
    graph_ = std::move(g);
    if (indexOf(selectedId_) < 0) selectedId_ = 0;
    lastRemember_.restart();
    fillWorld();
    refreshList();
    fillInspector();
    updateHistoryActions();
    applyEdit(0);
}

void SceneBuilder::updateHistoryActions() {
    undoAct_->setEnabled(!undo_.empty());
    redoAct_->setEnabled(!redo_.empty());
}

// ---------------------------------------------------------------------------
// Edit / play / stop
// ---------------------------------------------------------------------------
void SceneBuilder::setMode(Mode m) {
    mode_ = m;
    liveTargetValid_ = false;
    inspectorBody_->setEnabled(!sample_);
    playHint_->setVisible(!editing());
    playHint_->setText(sample_ ? "Идёт готовая сцена. Любая правка вернёт вас к своей сцене."
                               : "Идёт симуляция: роли и числа меняются на лету, на паузе работает гизмо. "
                                 "■ Стоп вернёт сцену, какой она была до ▶.");
    if (editing()) refreshView();
    refreshScenery();
    sendViewportFlags();
    updateGizmoTarget();
    emit modeChanged();
}

void SceneBuilder::play() {
    if (mode_ == Mode::Playing) return;
    if (editing()) loadIntoSimulation();
    ctrl_->setRunning(true);
    setMode(Mode::Playing);
}

void SceneBuilder::pause() {
    if (mode_ != Mode::Playing) return;
    ctrl_->setRunning(false);
    setMode(Mode::Paused);
}

// A ready-made scene goes back to its start; the builder's scene goes back to edit mode.
void SceneBuilder::stop() {
    if (sample_) {
        ctrl_->setRunning(false);
        ctrl_->post([](Simulation& s) { s.reset(); });
        setMode(Mode::Paused);
        return;
    }
    if (mode_ != Mode::Edit) returnToEdit(false);
}

void SceneBuilder::step() {
    if (editing()) {
        loadIntoSimulation();
        setMode(Mode::Paused);
    } else if (mode_ == Mode::Playing) {
        pause();
    }
    ctrl_->requestStep();
}

void SceneBuilder::showSample() {
    sample_ = true;
    selectedId_ = 0;
    fillInspector();
    setMode(Mode::Playing);
}

// Stop: the simulation goes; the graph comes back as it was before Play, as in Unity (unless an
// edit the running scene could not take is being kept). The history loses the steps made while
// playing.
void SceneBuilder::returnToEdit(bool keepEdits) {
    ctrl_->setRunning(false);
    ctrl_->post([](Simulation& s) { s.load(std::make_unique<GraphScene>(SceneGraph())); }); // the meta-objects go
    const bool restore = hasPlayBackup_ && !keepEdits && !sample_;
    if (restore) {
        const bool edited = playBackup_.save() != graph_.save();
        graph_ = playBackup_;
        undo_.resize(std::min(undo_.size(), undoAtPlay_));
        redo_.clear();
        if (indexOf(selectedId_) < 0) selectedId_ = 0;
        fillWorld();
        refreshList();
        fillInspector();
        updateHistoryActions();
        if (edited) emit statusMessage("Стоп: сцена вернулась к той, что была до ▶ (правки во время игры не сохраняются — как в Unity)");
    }
    hasPlayBackup_ = false;
    sample_ = false;
    setMode(Mode::Edit);
}

void SceneBuilder::loadIntoSimulation() {
    playBackup_ = simGraph_ = graph_;
    hasPlayBackup_ = true;
    undoAtPlay_ = undo_.size();
    const SceneGraph g = graph_;
    const std::string name = displayName();
    QPointer<SceneBuilder> self(this);
    ctrl_->post([g, name, self](Simulation& s) {
        auto scene = std::make_unique<GraphScene>(g);
        const GraphScene* built = scene.get();
        scene->name = name;
        s.load(std::move(scene)); // the scene stays alive inside the simulation
        std::vector<uint32_t> bodyEntity(s.rigid.bodies().size(), 0);
        for (size_t b = 0; b < bodyEntity.size(); ++b) bodyEntity[b] = built->entityIdOfBody(int(b));
        QMetaObject::invokeMethod(self, [self, bodyEntity] {
            if (self) self->onBodiesMapped(bodyEntity);
        }, Qt::QueuedConnection);
    });
}

std::string SceneBuilder::displayName() const {
    return fileName_.isEmpty() ? sceneName() : sceneName() + ": " + fileName_.toStdString();
}

void SceneBuilder::refreshScenery() {
    std::vector<RenderSnapshot::Body> scenery;
    if (!editing() && !sample_) {
        std::vector<uint32_t> ids;
        const auto snap = editView_.snapshot(graph_, displayName(), ids);
        for (size_t i = 0; i < ids.size(); ++i) { // everything that gets no body: role-less shapes, a burner, a nozzle
            const int e = indexOf(ids[i]);
            if (e >= 0 && !madeOfSomething(graph_.entities[size_t(e)]) && !graph_.entities[size_t(e)].magnet.enabled)
                scenery.push_back(snap->bodies[i]);
        }
    }
    emit sceneryBodies(std::move(scenery));
}

void SceneBuilder::refreshView() {
    if (!editing()) return;
    emit editSnapshot(editView_.snapshot(graph_, displayName(), bodyEntity_));
    sendViewportFlags();
    updateGizmoTarget();
}

void SceneBuilder::onBodiesMapped(const std::vector<uint32_t>& bodyEntity) {
    if (editing()) return; // stopped before the simulation was built: the edit view's map stays
    bodyEntity_ = bodyEntity;
    sendViewportFlags();
}

// The camera shows the things of the scene: the union of the visible, unlocked objects (the locked
// floor only when nothing else is there), with a margin - not the whole 4 x 3 x 4 m world box, in
// which small magnets would be dots. After that the camera is free until the next load or add.
void SceneBuilder::frameObjects() {
    AABB mine, all;
    for (const Entity& e : graph_.entities) {
        if (!e.visible) continue;
        const AABB box = editView_.worldBounds(graph_, e);
        all.expand(box);
        if (!e.locked) mine.expand(box);
    }
    AABB box = mine.valid() ? mine : all;
    if (!box.valid()) return;
    box.lo.y = std::min(box.lo.y, 0.0f); // the floor under them is in the picture
    const Vector3 minimum(1.2f);          // a lone 20 cm cube is not a close-up
    const Vector3 grow = vmax(minimum - box.extent(), Vector3(0.0f)) * 0.5f;
    const Vector3 margin(std::max(0.1f, 0.08f * maxComp(box.extent())));
    box = AABB(box.lo - grow - margin, box.hi + grow + margin);
    emit frameRequested(box);
}

// The viewport outlines the selected thing's bodies (and in edit mode the one under the mouse and
// the geometry without a role) and lets the mouse pass through locked ones.
void SceneBuilder::sendViewportFlags() {
    std::vector<int> selected, hovered, ghosts;
    std::vector<char> unpickable(bodyEntity_.size(), 0);
    for (size_t b = 0; b < bodyEntity_.size(); ++b) {
        const uint32_t id = bodyEntity_[b];
        const int i = indexOf(id);
        if (i < 0) continue;
        if (id == selectedId_) selected.push_back(int(b));
        else if (id == hoverId_ && editing()) hovered.push_back(int(b));
        if (editing() && entityIsGeometryOnly(graph_.entities[size_t(i)])) ghosts.push_back(int(b));
        unpickable[b] = graph_.entities[size_t(i)].locked;
    }
    emit highlightBodies(selected);
    emit hoverBodies(hovered);
    emit ghostBodies(ghosts);
    emit unpickableBodies(unpickable);
}

void SceneBuilder::updateGizmoTarget() {
    const Entity* e = selectedEntity();
    const bool editable = gizmoAllowed() && e && e->visible && !e->locked;
    const bool live = !editing() && liveTargetValid_ && !gizmoEntity_;
    emit gizmoTarget(editable, e ? (live ? liveTarget_ : e->position) : Vector3(0.0f),
                     e ? quaternionFromEulerDeg(e->rotationDeg) : Quaternion());
}

// On pause the gizmo sits where the selected object's body is now, not where it was put.
void SceneBuilder::onSimulationSnapshot(const RenderSnapshot& s) {
    if (editing() || sample_ || gizmoEntity_) return;
    const int body = bodyOfEntity(selectedId_);
    const bool valid = body >= 0 && body < int(s.bodies.size());
    const Vector3 at = valid ? s.bodies[size_t(body)].pos : Vector3(0.0f);
    if (valid == liveTargetValid_ && (!valid || (at.x == liveTarget_.x && at.y == liveTarget_.y && at.z == liveTarget_.z))) return;
    liveTargetValid_ = valid;
    liveTarget_ = at;
    updateGizmoTarget();
}

int SceneBuilder::bodyOfEntity(uint32_t id) const {
    for (size_t b = 0; b < bodyEntity_.size(); ++b)
        if (id && bodyEntity_[b] == id) return int(b);
    return -1;
}

uint32_t SceneBuilder::entityOfBody(int body) const {
    return body >= 0 && body < int(bodyEntity_.size()) ? bodyEntity_[size_t(body)] : 0;
}

// ---------------------------------------------------------------------------
// The mouse in the viewport
// ---------------------------------------------------------------------------
std::vector<PickHit> SceneBuilder::pickAll(const Ray& ray) {
    if (!editing()) return {};
    return editView_.pickAll(graph_, ray);
}

void SceneBuilder::onEntityClicked(uint32_t id) {
    if (!sample_) setSelected(id);
}

void SceneBuilder::onEntityHovered(uint32_t id) {
    if (id == hoverId_) return;
    hoverId_ = id;
    sendViewportFlags();
}

void SceneBuilder::selectBody(int body) {
    if (body < 0 || body >= int(bodyEntity_.size())) return;
    const int i = indexOf(bodyEntity_[size_t(body)]);
    if (i < 0 || graph_.entities[size_t(i)].locked) return;
    setSelected(graph_.entities[size_t(i)].id);
}

// ---------------------------------------------------------------------------
// Scenes and files
// ---------------------------------------------------------------------------
void SceneBuilder::newScene() {
    if (!editing()) returnToEdit(false);
    if (!graph_.entities.empty()) remember();
    graph_ = SceneGraph();
    fileName_.clear();
    Entity floor;
    floor.id = newId();
    floor.name = "Пол";
    floor.shape = ShapeKind::Plane;
    floor.size = Vector3(graph_.world.size.x, 0.02f, graph_.world.size.z);
    floor.position = Vector3(0.0f, 0.01f, 0.0f);
    floor.color = Vector3(0.52f, 0.55f, 0.60f);
    floor.rigid.enabled = true;
    floor.rigid.fixed = true;
    floor.locked = true; // clicks on the floor select nothing; it is still in the list
    graph_.entities.push_back(floor);
    selectedId_ = 0;
    fillWorld();
    refreshList();
    fillInspector();
    refreshView();
    frameObjects();
}

bool SceneBuilder::openFile(const QString& path, QString& error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = f.errorString();
        return false;
    }
    SceneGraph g;
    std::string err;
    if (!g.load(QString::fromUtf8(f.readAll()).toStdString(), err)) {
        error = QString::fromStdString(err);
        return false;
    }
    if (!editing()) returnToEdit(false);
    remember();
    graph_ = std::move(g);
    graph_.baseDirectory = QFileInfo(path).absolutePath().toStdString(); // model files next to the scene
    adoptIds();
    fileName_ = QFileInfo(path).fileName();
    selectedId_ = 0;
    fillWorld();
    refreshList();
    fillInspector();
    refreshView();
    frameObjects();
    return true;
}

bool SceneBuilder::saveFile(const QString& path, QString& error) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = f.errorString();
        return false;
    }
    f.write(QString::fromStdString(graph_.save()).toUtf8());
    return true;
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------
int SceneBuilder::indexOf(uint32_t id) const {
    if (id == 0) return -1;
    for (size_t i = 0; i < graph_.entities.size(); ++i)
        if (graph_.entities[i].id == id) return int(i);
    return -1;
}

Entity* SceneBuilder::selectedEntity() {
    const int i = indexOf(selectedId_);
    return i >= 0 ? &graph_.entities[size_t(i)] : nullptr;
}

uint32_t SceneBuilder::newId() { return nextId_++; }

// Files written by hand or by an older editor may have no ids, or the same id twice.
void SceneBuilder::adoptIds() {
    for (const Entity& e : graph_.entities) nextId_ = std::max(nextId_, e.id + 1);
    std::set<uint32_t> seen;
    for (Entity& e : graph_.entities) {
        if (e.id == 0 || seen.count(e.id)) e.id = newId();
        seen.insert(e.id);
    }
}
