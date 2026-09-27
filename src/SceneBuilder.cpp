// The scene builder's logic (see SceneBuilder.h): creating and removing things, turning roles on
// and off together with what they need, the undo history, sending the scene to the simulation,
// and the mouse (select, Shift+drag). The panels are built in SceneBuilderPanels.cpp.
#include "SceneBuilder.h"

#include "InspectorWidgets.h"
#include "ObjectInspector.h"
#include "RoleBar.h"

#include <QAction>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

using namespace rf;

namespace {

const char* kShapeNames[] = {"Куб", "Сфера", "Цилиндр", "Конус", "Плоскость"};

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

SceneBuilder::SceneBuilder(SimController* ctrl, QObject* parent) : QObject(parent), ctrl_(ctrl) {
    reloadTimer_ = new QTimer(this);
    reloadTimer_->setSingleShot(true);
    reloadTimer_->setInterval(150); // dragging a number or an object reloads a few times a second, not 60
    connect(reloadTimer_, &QTimer::timeout, this, &SceneBuilder::reload);
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
// Creating and removing
// ---------------------------------------------------------------------------
void SceneBuilder::addEntity(ShapeKind shape) {
    remember();
    Entity e;
    e.id = newId();
    e.shape = shape;
    int same = 0;
    for (const Entity& o : graph_.entities) same += o.shape == shape;
    e.name = QString("%1 %2").arg(kShapeNames[int(shape)]).arg(same + 1).toStdString();
    e.color = kPalette[graph_.entities.size() % 7];
    e.rigid.enabled = true;
    e.rigid.restitution = 0.45f; // lively: a new shape visibly bounces
    if (shape == ShapeKind::Plane) {
        e.size = Vector3(3.0f, 0.02f, 3.0f);
        e.position = Vector3(0.0f, 0.3f, 0.0f);
        e.color = Vector3(0.62f, 0.64f, 0.68f);
        e.rigid.fixed = true;
    } else {
        // A free spot on a ring of places around the centre, 1 m above the floor: it drops at once.
        for (int k = 0; k < 25; ++k) {
            const Vector3 p(0.45f * float(k % 5 - 2), 1.0f, 0.45f * float(k / 5 - 2));
            const bool taken = std::any_of(graph_.entities.begin(), graph_.entities.end(), [&](const Entity& o) {
                return o.shape != ShapeKind::Plane && std::hypot(o.position.x - p.x, o.position.z - p.z) < 0.3f;
            });
            e.position = p;
            if (!taken) break;
        }
    }
    graph_.entities.push_back(e);
    setSelected(e.id);
    refreshList();
    scheduleReload();
    frameObjects();
    emit wantsRunning();
}

void SceneBuilder::removeSelected() {
    const int i = indexOf(selectedId_);
    if (i < 0) return;
    remember();
    graph_.entities.erase(graph_.entities.begin() + i);
    const int next = std::min(i, int(graph_.entities.size()) - 1);
    selectedId_ = next >= 0 ? graph_.entities[size_t(next)].id : 0;
    refreshList();
    fillInspector();
    scheduleReload();
}

void SceneBuilder::duplicateSelected() {
    const Entity* s = selectedEntity();
    if (!s) return;
    remember();
    Entity e = *s;
    e.id = newId();
    e.name += " копия";
    e.position.y += e.size.y + 0.05f; // on top of the original: it falls onto it
    e.locked = false;
    graph_.entities.push_back(e);
    setSelected(e.id);
    refreshList();
    scheduleReload();
    frameObjects();
}

// ---------------------------------------------------------------------------
// Roles: one click turns the shape into a model; the role switches on what it needs
// ---------------------------------------------------------------------------
void SceneBuilder::toggleRole(RoleIcon role) {
    Entity* e = selectedEntity();
    if (!e) {
        emit statusMessage("Сначала выберите объект — или создайте его кнопкой сверху");
        return;
    }
    remember();
    const bool on = !roleEnabled(*e, role);
    if (on && isMadeOfRole(role)) // made of one thing at a time
        for (RoleIcon other : {RoleIcon::Rigid, RoleIcon::Soft, RoleIcon::Liquid, RoleIcon::Cloth}) setRole(*e, other, false);
    setRole(*e, role, on);
    if (on) switchOnWhatRoleNeeds(*e, role);
    fillInspector();
    refreshList();
    scheduleReload();
}

// The magic: a role never needs a second click somewhere else to work.
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
        emit statusMessage("Ткань висит на пруте; края закрепляются в разделе «Ткань»");
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
    Entity* e = selectedEntity();
    if (filling_ || !e) return;
    remember(true);
    object_->writeTo(*e);
    refreshList();
    refreshHeader();
    sendViewportFlags(); // the lock may have changed
    scheduleReload();
}

void SceneBuilder::onDetailsEdited() {
    Entity* e = selectedEntity();
    if (filling_ || !e) return;
    remember(true);
    readDetails(*e);
    refreshList();
    refreshHeader();
    scheduleReload();
}

void SceneBuilder::onWorldEdited() {
    if (filling_) return;
    remember(true);
    graph_.world.gravity = gravity_->value();
    graph_.world.size = worldSize_->value();
    graph_.world.gas = gas_->isChecked();
    graph_.world.magneticGas = plasma_->isChecked();
    scheduleReload();
}

// The eye and the lock are clicked right in the list; any other column selects the row.
void SceneBuilder::onListClicked(QTreeWidgetItem* item, int column) {
    const uint32_t id = item->data(0, Qt::UserRole).toUInt();
    const int i = indexOf(id);
    if (i < 0) return;
    Entity& e = graph_.entities[size_t(i)];
    if (column == 1 || column == 2) {
        remember();
        if (column == 1) e.visible = !e.visible;
        else e.locked = !e.locked;
        refreshList();
        fillInspector();
        sendViewportFlags();
        if (column == 1) scheduleReload(); // hidden things leave the simulation
        return;
    }
    setSelected(id);
}

void SceneBuilder::setSelected(uint32_t id) {
    selectedId_ = indexOf(id) >= 0 ? id : 0;
    for (int r = 0; r < list_->topLevelItemCount(); ++r) {
        QTreeWidgetItem* it = list_->topLevelItem(r);
        if (it->data(0, Qt::UserRole).toUInt() == selectedId_) {
            const QSignalBlocker quiet(list_);
            list_->setCurrentItem(it);
        }
    }
    fillInspector();
    sendViewportFlags();
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
    redo_.push_back(graph_);
    SceneGraph g = std::move(undo_.back());
    undo_.pop_back();
    restore(std::move(g));
}

void SceneBuilder::redo() {
    if (redo_.empty()) return;
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
    reload();
}

void SceneBuilder::updateHistoryActions() {
    undoAct_->setEnabled(!undo_.empty());
    redoAct_->setEnabled(!redo_.empty());
}

// ---------------------------------------------------------------------------
// The simulation: the graph as a running scene, and which body belongs to which thing
// ---------------------------------------------------------------------------
void SceneBuilder::scheduleReload() { reloadTimer_->start(); }

void SceneBuilder::reload() {
    reloadTimer_->stop();
    const SceneGraph g = graph_;
    std::vector<uint32_t> ids;
    for (const Entity& e : g.entities) ids.push_back(e.id);
    const std::string name = fileName_.isEmpty() ? sceneName() : sceneName() + ": " + fileName_.toStdString();
    QPointer<SceneBuilder> self(this);
    ctrl_->post([g, ids, name, self](Simulation& s) {
        auto scene = std::make_unique<GraphScene>(g);
        const GraphScene* built = scene.get();
        scene->name = name;
        s.load(std::move(scene)); // the scene stays alive inside the simulation
        std::vector<uint32_t> bodyEntity(s.rigid.bodies().size(), 0);
        for (size_t b = 0; b < bodyEntity.size(); ++b) {
            const int i = built->entityOfBody(int(b));
            if (i >= 0 && i < int(ids.size())) bodyEntity[b] = ids[size_t(i)];
        }
        QMetaObject::invokeMethod(self, [self, bodyEntity] {
            if (self) self->onBodiesMapped(bodyEntity);
        }, Qt::QueuedConnection);
    });
}

// The camera shows the things of the scene: the union of the visible, unlocked objects (the locked
// floor only when nothing else is there), with a margin - not the whole 4 x 3 x 4 m world box, in
// which small magnets would be dots. After that the camera is free until the next load or add.
void SceneBuilder::frameObjects() {
    AABB mine, all;
    for (const Entity& e : graph_.entities) {
        if (!e.visible) continue;
        const float r = 0.5f * length(e.size); // any rotation fits in this sphere
        const AABB box(e.position - Vector3(r), e.position + Vector3(r));
        all.expand(box);
        if (!e.locked) mine.expand(box);
    }
    AABB box = mine.valid() ? mine : all;
    if (!box.valid()) return;
    box.lo.y = std::min(box.lo.y, 0.0f); // things fall: the floor under them is in the picture
    const Vector3 minimum(1.2f);          // a lone 20 cm cube is not a close-up
    const Vector3 grow = vmax(minimum - box.extent(), Vector3(0.0f)) * 0.5f;
    const Vector3 margin(std::max(0.1f, 0.08f * maxComp(box.extent())));
    box = AABB(box.lo - grow - margin, box.hi + grow + margin);
    emit frameRequested(box);
}

void SceneBuilder::onBodiesMapped(const std::vector<uint32_t>& bodyEntity) {
    bodyEntity_ = bodyEntity;
    sendViewportFlags();
}

// The viewport outlines the selected thing's bodies and lets the mouse pass through locked ones.
void SceneBuilder::sendViewportFlags() {
    std::vector<int> selected;
    std::vector<char> unpickable(bodyEntity_.size(), 0);
    for (size_t b = 0; b < bodyEntity_.size(); ++b) {
        const uint32_t id = bodyEntity_[b];
        if (id != 0 && id == selectedId_) selected.push_back(int(b));
        const int i = indexOf(id);
        unpickable[b] = i >= 0 && graph_.entities[size_t(i)].locked;
    }
    emit highlightBodies(selected);
    emit unpickableBodies(unpickable);
}

// ---------------------------------------------------------------------------
// The mouse in the viewport
// ---------------------------------------------------------------------------
void SceneBuilder::selectBody(int body) {
    if (body < 0 || body >= int(bodyEntity_.size())) return;
    const int i = indexOf(bodyEntity_[size_t(body)]);
    if (i < 0 || graph_.entities[size_t(i)].locked) return;
    setSelected(graph_.entities[size_t(i)].id);
}

void SceneBuilder::moveStarted(int body, Vector3 hit) {
    if (body < 0 || body >= int(bodyEntity_.size())) return;
    const int i = indexOf(bodyEntity_[size_t(body)]);
    if (i < 0 || graph_.entities[size_t(i)].locked) return;
    remember();
    setSelected(graph_.entities[size_t(i)].id);
    movingId_ = selectedId_;
    moveStartPosition_ = graph_.entities[size_t(i)].position;
    moveStartHit_ = hit;
}

void SceneBuilder::moveDragged(Vector3 point) {
    const int i = indexOf(movingId_);
    if (i < 0) return;
    Entity& e = graph_.entities[size_t(i)];
    const Vector3 d = point - moveStartHit_;
    e.position = moveStartPosition_ + Vector3(d.x, 0.0f, d.z); // across the floor, height kept
    object_->setPosition(e.position);
    scheduleReload();
}

void SceneBuilder::moveFinished() {
    if (!movingId_) return;
    movingId_ = 0;
    reload();
}

// ---------------------------------------------------------------------------
// Scenes and files
// ---------------------------------------------------------------------------
void SceneBuilder::newScene() {
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
    frameObjects();
    reload();
    emit wantsRunning();
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
    remember();
    graph_ = std::move(g);
    adoptIds();
    fileName_ = QFileInfo(path).fileName();
    selectedId_ = 0;
    for (const Entity& e : graph_.entities)
        if (!e.locked) {
            selectedId_ = e.id;
            break;
        }
    fillWorld();
    refreshList();
    fillInspector();
    frameObjects();
    reload();
    emit wantsRunning();
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
