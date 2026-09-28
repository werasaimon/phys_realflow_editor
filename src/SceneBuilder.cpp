// The scene builder's logic (see SceneBuilder.h): creating and removing things, turning roles on
// and off together with what they need, the gizmo's edits, the undo history, the edit / play / stop
// modes and what the viewport shows in each. The panels are built in SceneBuilderPanels.cpp.
#include "SceneBuilder.h"

#include "InspectorWidgets.h"
#include "ObjectInspector.h"
#include "RoleBar.h"

#include <QAction>
#include <QApplication>
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
const float kSpawnHeight = 0.8f; // a new shape appears this high over the floor (its bottom)
const Vector3 kPalette[] = {{0.91f, 0.50f, 0.28f}, {0.31f, 0.59f, 0.92f}, {0.43f, 0.77f, 0.38f}, {0.93f, 0.77f, 0.31f},
                            {0.72f, 0.47f, 0.88f}, {0.33f, 0.80f, 0.80f}, {0.93f, 0.42f, 0.55f}};

bool madeOfSomething(const Entity& e) { return e.rigid.enabled || e.soft.enabled || e.liquid.enabled || e.cloth.enabled; }

} // namespace

const char* shapeName(ShapeKind k) {
    static const char* names[] = {"Куб", "Сфера", "Цилиндр", "Конус", "Плоскость", "Модель"};
    return names[std::clamp(int(k), 0, 5)];
}

SceneBuilder::SceneBuilder(SimController* ctrl, QObject* parent) : QObject(parent), ctrl_(ctrl) {
    lastRemember_.start();
    buildActions();
    buildLightActions();
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
std::string arrayText(const ArrayObject& a) {
    SceneGraph one;
    one.arrays.push_back(a);
    return one.save();
}
std::string worldText(const SceneGraph& g) {
    SceneGraph world;
    world.world = g.world;
    return world.save();
}
std::string lightsText(const SceneGraph& g) {
    SceneGraph lights;
    lights.lights = g.lights;
    lights.cameras = g.cameras;
    return lights.save();
}
std::string groupsText(const SceneGraph& g) {
    SceneGraph groups;
    groups.groups = g.groups;
    return groups.save();
}
bool samePose(const Entity& a, const Entity& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.rotationDeg.x == b.rotationDeg.x && a.rotationDeg.y == b.rotationDeg.y && a.rotationDeg.z == b.rotationDeg.z;
}

// What an edit changed, compared with what the simulation was last given.
struct PlayChanges {
    std::vector<std::pair<Entity, bool>> entities; // the entity and whether the editor moved it
    std::vector<uint32_t> removed;
    std::vector<ArrayObject> arrays;               // changed or new; a removed one with no copies
    bool empty() const { return entities.empty() && removed.empty() && arrays.empty(); }
};

PlayChanges playChanges(const SceneGraph& now, const SceneGraph& before) {
    PlayChanges c;
    for (const Entity& e : now.entities) {
        const Entity* old = nullptr;
        for (const Entity& s : before.entities)
            if (s.id == e.id) old = &s;
        if (!old || entityText(*old) != entityText(e)) c.entities.push_back({e, !old || !samePose(*old, e)});
    }
    for (const Entity& s : before.entities)
        if (std::none_of(now.entities.begin(), now.entities.end(), [&](const Entity& e) { return e.id == s.id; })) c.removed.push_back(s.id);
    for (const ArrayObject& a : now.arrays) {
        const ArrayObject* old = nullptr;
        for (const ArrayObject& s : before.arrays)
            if (s.id == a.id) old = &s;
        if (!old || arrayText(*old) != arrayText(a)) c.arrays.push_back(a);
    }
    for (ArrayObject gone : before.arrays) {
        if (std::any_of(now.arrays.begin(), now.arrays.end(), [&](const ArrayObject& a) { return a.id == gone.id; })) continue;
        gone.count[0] = gone.count[1] = gone.count[2] = 0; // no copies: its bodies go
        c.arrays.push_back(gone);
    }
    return c;
}
} // namespace

// An edit while the simulation runs: the objects that differ from what the simulation was last
// given are rebuilt in place between frames (added, removed), the rest runs on untouched. An object
// the editor did not move keeps its live pose and velocity (the simulation's own pose is sent back,
// so the SDK sees it unmoved). An array is rebuilt as a whole (its copies that stay keep moving).
// The world itself and the groups need a reload: the builder stops and says why.
void SceneBuilder::applyEditDuringPlay(uint32_t entityId) {
    (void)entityId; // every changed object is found by comparing, whichever edit made it
    if (worldText(graph_) != worldText(simGraph_))
        return reloadNeeded("Тяжесть, коробку и газ меняют только перезапуском сцены");
    if (groupsText(graph_) != groupsText(simGraph_)) return reloadNeeded("Группы меняют только перезапуском сцены");
    refreshLightsAndCameras();
    if (lightsText(graph_) != lightsText(simGraph_)) { // the physics never sees them: the next snapshot shows them
        const std::vector<Light> lights = graph_.lights;
        const std::vector<Camera> cameras = graph_.cameras;
        ctrl_->post([lights, cameras](Simulation& s) {
            if (auto* scene = dynamic_cast<GraphScene*>(s.scene())) scene->setLightsAndCameras(lights, cameras);
        });
    }
    const PlayChanges changes = playChanges(graph_, simGraph_);
    simGraph_ = graph_;
    refreshScenery(); // a shape that lost (or has no) role is still drawn
    if (changes.empty()) return;
    QPointer<SceneBuilder> self(this);
    ctrl_->post([changes, self](Simulation& s) {
        auto* scene = dynamic_cast<GraphScene*>(s.scene());
        if (!scene) return;
        bool ok = true;
        for (uint32_t id : changes.removed) scene->removeEntity(s, id);
        std::vector<std::pair<uint32_t, std::pair<Vector3, Vector3>>> poses;
        for (const auto& [entity, moved] : changes.entities) {
            Entity e = entity;
            for (const Entity& held : scene->graph().entities) // unmoved: the pose the scene holds, so it keeps the live one
                if (held.id == e.id && !moved) e.position = held.position, e.rotationDeg = held.rotationDeg;
            ok = scene->rebuildEntity(s, e) && ok;
            for (const Entity& held : scene->graph().entities)
                if (held.id == e.id) poses.push_back({e.id, {held.position, held.rotationDeg}});
        }
        for (const ArrayObject& a : changes.arrays) ok = scene->rebuildArray(s, a) && ok;
        std::vector<uint32_t> bodyEntity(s.rigid.bodies().size(), 0);
        for (size_t b = 0; b < bodyEntity.size(); ++b) bodyEntity[b] = scene->objectIdOfBody(int(b));
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
    if (const SceneObject* o = selectedObject()) showTransform(*o);
    if (!ok) reloadNeeded("Включение газа требует перезапуска сцены");
}

// A change the running simulation cannot take: stop with the edit kept, and say what to do.
void SceneBuilder::reloadNeeded(const QString& why) {
    returnToEdit(true);
    showBanner(why + ". Правка сохранена.", "▶ Пуск", [this] { play(); });
    stickyBanner_ = true;
}

void SceneBuilder::showBanner(const QString& text, const QString& buttonText, std::function<void()> fix) {
    banner_->showMessage(text, buttonText, std::move(fix));
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
        e.position.y = kSpawnHeight + 0.5f * e.size.y; // in the air: given a body and ▶, it falls
    }
    graph_.entities.push_back(e);
    setSelected(e.id);
    refreshList();
    applyEdit(e.id);
    frameObjects();
    emit firstAction();
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

// The eye and the lock of any object (the list, the context menu).
void SceneBuilder::toggleVisible(uint32_t id) {
    if (!objectById(id)) return;
    prepareEdit(id);
    remember();
    objectById(id)->visible = !objectById(id)->visible;
    refreshList();
    fillInspector();
    applyEdit(id);
}

void SceneBuilder::toggleLocked(uint32_t id) {
    if (!objectById(id)) return;
    prepareEdit(id);
    remember();
    objectById(id)->locked = !objectById(id)->locked;
    refreshList();
    if (objectById(id)->locked && isSelected(id)) setSelected(0); // a locked thing is not edited
    else fillInspector();
    applyEdit(id);
}

void SceneBuilder::frameSelected() {
    AABB box;
    for (uint32_t id : selection_) box.expand(boundsOf(id));
    if (!box.valid()) return frameObjects();
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
// On every selected shape at once (an instance: its master, so all its instances): added to all
// unless all have it, then taken from all.
void SceneBuilder::toggleRole(RoleIcon role) {
    if (selectedEntities().empty()) {
        emit statusMessage("Сначала выберите объект — или создайте его кнопкой сверху");
        return;
    }
    prepareEdit(selectedId_);
    remember();
    std::vector<uint32_t> masters;
    for (Entity* e : selectedEntities())
        if (std::find(masters.begin(), masters.end(), masterOf(e->id)) == masters.end()) masters.push_back(masterOf(e->id));
    const bool on = !std::all_of(masters.begin(), masters.end(), [&](uint32_t id) { return roleEnabled(*entityById(id), role); });
    for (uint32_t id : masters) {
        Entity& e = *entityById(id);
        if (on && isMadeOfRole(role)) // made of one thing at a time
            for (RoleIcon other : {RoleIcon::Rigid, RoleIcon::Soft, RoleIcon::Liquid, RoleIcon::Cloth}) setRole(e, other, false);
        setRole(e, role, on);
        keepComponentsConsistent(e, role, on);
        if (on) switchOnWhatRoleNeeds(e, role);
    }
    fillInspector();
    refreshList();
    applyEdit(selectedId_);
    emit firstAction();
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
        if (!madeOfSomething(e)) { // a magnet needs a body to push, and the body a collider
            e.rigid.enabled = true;
            e.rigid.fixed = false;
            e.collider.enabled = true;
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

// The collider pairs with a rigid body or stands alone (a fixed obstacle), as in Unity: adding
// "Твёрдое тело" adds a collider "Авто"; a soft body, liquid or cloth has no rigid collider.
void SceneBuilder::keepComponentsConsistent(Entity& e, RoleIcon role, bool on) {
    if (!on) return;
    if (role == RoleIcon::Rigid && !e.collider.enabled) {
        e.collider.enabled = true;
        e.collider.kind = ColliderKind::Auto;
        emit statusMessage("Добавлен коллайдер «Авто»: тело сталкивается своей формой");
    }
    if (role == RoleIcon::Collider)
        for (RoleIcon other : {RoleIcon::Soft, RoleIcon::Liquid, RoleIcon::Cloth}) setRole(e, other, false);
    if (role == RoleIcon::Soft || role == RoleIcon::Liquid || role == RoleIcon::Cloth) e.collider.enabled = false;
}

// ---------------------------------------------------------------------------
// Edits from the widgets
// ---------------------------------------------------------------------------
void SceneBuilder::onObjectEdited() { applyWidgetEdit(false); }
void SceneBuilder::onDetailsEdited() { applyWidgetEdit(true); }

// A number dragged or typed merges its small steps into one undo step; a material is one click and
// always a step of its own (merged, «Резина» right after «Мягкое» took the soft body away on undo).
// The step is opened here; the edit's own remember(true) then falls into it.
void SceneBuilder::onSoftPresetChosen() {
    if (filling_ || selection_.empty()) return;
    prepareEdit(selectedId_);
    remember();
    applyWidgetEdit(true);
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
    if (!objectById(id)) return;
    if (column == 1) return toggleVisible(id);
    if (column == 2) {
        prepareEdit(id);
        remember();
        objectById(id)->locked = !objectById(id)->locked;
        refreshList();
        fillInspector();
        applyEdit(id);
        return;
    }
    if (QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) toggleSelected(id);
    else setSelected(id);
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
    pruneSelection();
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
    else refreshColliderGuides(); // the play-mode lines come with the next snapshot
    refreshLightsAndCameras();
    refreshScenery();
    sendViewportFlags();
    updateGizmoTarget();
    emit modeChanged();
}

void SceneBuilder::play() {
    if (mode_ == Mode::Playing) return;
    stickyBanner_ = false;
    refreshBanner();
    emit firstAction();
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
        if (hasKeptBefore_) undo_.push_back(keptBefore_); // K: one Ctrl+Z puts the objects back
        redo_.clear();
        pruneSelection();
        fillWorld();
        refreshList();
        fillInspector();
        updateHistoryActions();
        if (edited) emit statusMessage("Стоп: сцена вернулась к той, что была до ▶ (правки во время игры не сохраняются — как в Unity)");
    }
    hasPlayBackup_ = false;
    hasKeptBefore_ = false;
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
        for (size_t b = 0; b < bodyEntity.size(); ++b) bodyEntity[b] = built->objectIdOfBody(int(b));
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
        std::map<uint32_t, bool> noBody; // everything that gets no body: role-less shapes, a burner, a nozzle
        for (const Entity& w : worldEntities(graph_)) noBody[w.id] = !madeOfSomething(w) && !w.magnet.enabled;
        for (size_t i = 0; i < ids.size(); ++i)
            if (noBody[ids[i]]) scenery.push_back(snap->bodies[i]);
    }
    emit sceneryBodies(std::move(scenery));
}

void SceneBuilder::refreshView() {
    if (!editing()) return;
    emit editSnapshot(editView_.snapshot(graph_, displayName(), bodyEntity_));
    sendViewportFlags();
    updateGizmoTarget();
    refreshColliderGuides();
    refreshLightsAndCameras();
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
    for (const Entity& w : worldEntities(graph_)) {
        if (!w.visible) continue;
        const AABB box = editView_.worldBounds(graph_, w);
        all.expand(box);
        if (!w.locked) mine.expand(box);
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
// A body belongs to what its drawn id says: a shape, an array's copy, a glued group (while playing);
// it is outlined when that, or a group or an array it is in, is selected.
void SceneBuilder::sendViewportFlags() {
    std::vector<int> selected, hovered, ghosts;
    std::vector<char> unpickable(bodyEntity_.size(), 0);
    std::map<uint32_t, const Entity*> drawn;
    const std::vector<Entity> world = worldEntities(graph_);
    for (const Entity& w : world) drawn[w.id] = &w;
    for (size_t b = 0; b < bodyEntity_.size(); ++b) {
        const uint32_t id = bodyEntity_[b];
        const SceneObject* owner = findObject(graph_, ownerOf(id));
        if (!owner) continue;
        if (drawnSelected(id)) selected.push_back(int(b));
        else if (editing() && isInside(ownerOf(id), hoverId_)) hovered.push_back(int(b));
        const auto w = drawn.find(id);
        if (editing() && w != drawn.end() && entityIsGeometryOnly(*w->second)) ghosts.push_back(int(b));
        unpickable[b] = w != drawn.end() ? w->second->locked : owner->locked;
    }
    emit highlightBodies(selected);
    emit hoverBodies(hovered);
    emit ghostBodies(ghosts);
    emit unpickableBodies(unpickable);
}

// The gizmo sits on the active object's world pose (a group: its centre, an array: its first copy).
void SceneBuilder::updateGizmoTarget() {
    const SceneObject* o = findObject(graph_, selectedId_);
    const bool editable = gizmoAllowed() && o && effectivelyVisible(graph_, *o) && !o->locked;
    const bool live = !editing() && liveTargetValid_ && !gizmoEntity_;
    Vector3 p(0.0f);
    Quaternion q;
    if (o) worldPose(graph_, *o, p, q);
    emit gizmoTarget(editable, live ? liveTarget_ : p, q);
}

// On pause the gizmo sits where the selected object's body is now, not where it was put.
void SceneBuilder::onSimulationSnapshot(const RenderSnapshot& s) {
    if (!editing() && !sample_) playColliderGuides(s);
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
// Each hit is what a click there selects (a group before its members), each thing once, nearest first.
std::vector<PickHit> SceneBuilder::pickAll(const Ray& ray) {
    if (!editing()) return {};
    std::vector<PickHit> hits, all = editView_.pickAll(graph_, ray);
    for (const PickHit& h : pickMarkers(ray)) all.push_back(h); // lights and cameras: their wireframes
    std::sort(all.begin(), all.end(), [](const PickHit& a, const PickHit& b) { return a.t < b.t; });
    for (PickHit h : all) {
        h.id = clickTarget(h.id);
        if (h.id == 0 || std::any_of(hits.begin(), hits.end(), [&](const PickHit& o) { return o.id == h.id; })) continue;
        h.name = objectTitle(h.id).toStdString();
        hits.push_back(h);
    }
    return hits;
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
    const uint32_t id = clickTarget(bodyEntity_[size_t(body)]);
    const SceneObject* o = findObject(graph_, id);
    if (!o || o->locked) return;
    setSelected(id);
}

// ---------------------------------------------------------------------------
// Scenes and files
// ---------------------------------------------------------------------------
void SceneBuilder::newScene() {
    if (!editing()) returnToEdit(false);
    if (!graph_.entities.empty()) remember();
    graph_ = SceneGraph();
    selection_.clear();
    fileName_.clear();
    throughId_ = 0;
    Entity floor;
    floor.id = newId();
    floor.name = "Пол";
    floor.shape = ShapeKind::Plane;
    floor.size = Vector3(graph_.world.size.x, 0.02f, graph_.world.size.z);
    floor.position = Vector3(0.0f, 0.01f, 0.0f);
    floor.color = Vector3(0.52f, 0.55f, 0.60f);
    floor.collider.enabled = true; // a collider alone: a fixed obstacle, as Unity's static colliders
    floor.locked = true;           // clicks on the floor select nothing; it is still in the list
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
    throughId_ = 0;
    selection_.clear();
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

// After an undo or a Stop: what is gone from the graph is gone from the selection too.
void SceneBuilder::pruneSelection() {
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [this](uint32_t id) { return !findObject(graph_, id); }),
                     selection_.end());
    if (!isSelected(selectedId_)) selectedId_ = selection_.empty() ? 0 : selection_.back();
}

// Files written by hand or by an older editor may have no ids, or the same id twice.
void SceneBuilder::adoptIds() {
    nextId_ = std::max(nextId_, nextId(graph_)); // groups and arrays too
    std::set<uint32_t> seen;
    for (Entity& e : graph_.entities) {
        if (e.id == 0 || seen.count(e.id)) e.id = newId();
        seen.insert(e.id);
    }
}
