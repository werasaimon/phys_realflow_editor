// Many objects at once in the scene builder (see SceneBuilder.h): the lookups that treat a shape, a
// group and an array alike, the hierarchy (which group a thing is in, what a click selects), groups
// (Ctrl+G / Ctrl+Shift+G), arrays ("Сделать массивом…", "Разобрать на объекты"), instances
// ("Отвязать") and the selection helpers of the menus ("Выбрать все такие же", "Выбрать все с
// компонентом", "Инвертировать выбор"). The poses are kept in the world whatever the hierarchy
// does: grouping, ungrouping or exploding an array never moves anything on the screen.
#include "SceneBuilder.h"

#include "RoleBar.h"

#include <QString>

#include <algorithm>
#include <set>

using namespace rf;

namespace {

bool contains(const std::vector<uint32_t>& ids, uint32_t id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

// The components of a shape as bits (RoleIcon order), to compare two shapes at a glance.
int roleBits(const Entity& e) {
    int bits = 0;
    for (int k = 0; k < int(RoleIcon::Count); ++k) bits |= roleEnabled(e, RoleIcon(k)) ? 1 << k : 0;
    return bits;
}

} // namespace

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------
SceneObject* SceneBuilder::objectById(uint32_t id) {
    if (Entity* e = entityById(id)) return e;
    if (Group* g = groupById(id)) return g;
    if (Light* l = lightById(id)) return l;
    if (Camera* c = cameraById(id)) return c;
    return arrayById(id);
}

Entity* SceneBuilder::entityById(uint32_t id) {
    const int i = indexOf(id);
    return i >= 0 ? &graph_.entities[size_t(i)] : nullptr;
}

Group* SceneBuilder::groupById(uint32_t id) {
    for (Group& g : graph_.groups)
        if (id != 0 && g.id == id) return &g;
    return nullptr;
}

ArrayObject* SceneBuilder::arrayById(uint32_t id) {
    for (ArrayObject& a : graph_.arrays)
        if (id != 0 && a.id == id) return &a;
    return nullptr;
}

SceneObject* SceneBuilder::selectedObject() { return objectById(selectedId_); }

uint32_t SceneBuilder::ownerOf(uint32_t drawnId) const {
    if (!isArrayCopyId(drawnId)) return drawnId;
    for (const ArrayObject& a : graph_.arrays)
        if ((a.id & 0x7FFFu) == arrayOfCopyId(drawnId)) return a.id;
    return 0;
}

std::vector<uint32_t> SceneBuilder::chainOf(uint32_t id) const {
    std::vector<uint32_t> chain;
    for (const SceneObject* o = findObject(graph_, id); o && !contains(chain, o->id); o = findObject(graph_, o->parent))
        chain.push_back(o->id);
    return chain;
}

bool SceneBuilder::isInside(uint32_t id, uint32_t ancestor) const { return ancestor != 0 && contains(chainOf(id), ancestor); }

std::vector<uint32_t> SceneBuilder::childrenOf(uint32_t id) const {
    std::vector<uint32_t> kids;
    for (const Entity& e : graph_.entities)
        if (e.parent == id) kids.push_back(e.id);
    for (const Group& g : graph_.groups)
        if (g.parent == id) kids.push_back(g.id);
    for (const ArrayObject& a : graph_.arrays)
        if (a.parent == id) kids.push_back(a.id);
    for (const Light& l : graph_.lights)
        if (l.parent == id) kids.push_back(l.id);
    for (const Camera& c : graph_.cameras)
        if (c.parent == id) kids.push_back(c.id);
    return kids;
}

// What the gizmo, Delete and Ctrl+D work on: a member of a selected group goes with its group.
std::vector<uint32_t> SceneBuilder::selectionRoots() const {
    std::vector<uint32_t> roots;
    for (uint32_t id : selection_) {
        const SceneObject* o = findObject(graph_, id);
        if (!o || o->locked) continue;
        const std::vector<uint32_t> chain = chainOf(id);
        if (std::none_of(chain.begin() + 1, chain.end(), [this](uint32_t up) { return isSelected(up); })) roots.push_back(id);
    }
    return roots;
}

std::vector<uint32_t> SceneBuilder::topLevelObjects() const {
    std::vector<uint32_t> ids;
    auto add = [&](const SceneObject& o) {
        if (o.visible && !o.locked && !findObject(graph_, o.parent)) ids.push_back(o.id);
    };
    for (const Entity& e : graph_.entities) add(e);
    for (const Group& g : graph_.groups) add(g);
    for (const ArrayObject& a : graph_.arrays) add(a);
    for (const Light& l : graph_.lights) add(l);
    for (const Camera& c : graph_.cameras) add(c);
    return ids;
}

bool SceneBuilder::isHiddenTemplate(const Entity& e) const {
    return !e.visible && std::any_of(graph_.arrays.begin(), graph_.arrays.end(), [&](const ArrayObject& a) { return a.templateId == e.id; });
}

uint32_t SceneBuilder::masterOf(uint32_t entityId) const {
    std::vector<uint32_t> seen{entityId};
    uint32_t id = entityId;
    for (;;) { // follow instanceOf to the end, as rf::resolveInstance does (a cycle or a missing master ends it)
        const int i = indexOf(id);
        const uint32_t next = i >= 0 ? graph_.entities[size_t(i)].instanceOf : 0;
        if (next == 0 || contains(seen, next) || indexOf(next) < 0) return id;
        id = next;
        seen.push_back(id);
    }
}

QString SceneBuilder::objectTitle(uint32_t id) const {
    const SceneObject* o = findObject(graph_, id);
    if (!o) return QString();
    for (const ArrayObject& a : graph_.arrays) {
        if (a.id != id) continue;
        const SceneObject* t = findObject(graph_, a.templateId);
        const int n = a.pattern == ArrayPattern::Grid ? a.count[0] * a.count[1] * a.count[2] : a.count[0];
        return QString("Массив: %1 × %2").arg(n).arg(t ? QString::fromStdString(t->name) : QString("?"));
    }
    return QString::fromStdString(o->name);
}

// ---------------------------------------------------------------------------
// Where things are
// ---------------------------------------------------------------------------
AABB SceneBuilder::boundsOf(uint32_t id) {
    AABB box;
    for (const Entity& w : worldEntities(graph_))
        if (w.visible && isInside(ownerOf(w.id), id)) box.expand(editView_.worldBounds(graph_, w));
    auto mark = [&](const SceneObject& o) { // a light or a camera: about the size of its wireframe
        const Vector3 p = worldPositionOf(o.id);
        if (effectivelyVisible(graph_, o) && isInside(o.id, id)) box.expand(AABB(p - Vector3(0.3f), p + Vector3(0.3f)));
    };
    for (const Light& l : graph_.lights) mark(l);
    for (const Camera& c : graph_.cameras) mark(c);
    return box;
}

Vector3 SceneBuilder::worldPositionOf(uint32_t id) const {
    const SceneObject* o = findObject(graph_, id);
    Vector3 p(0.0f);
    Quaternion q;
    if (o) worldPose(graph_, *o, p, q);
    return p;
}

// The first click on a grouped thing selects its topmost group; the next click on it, the group one
// level down, and so on to the thing itself (Unity's and Blender's rule). An array is one thing.
uint32_t SceneBuilder::clickTarget(uint32_t drawnId) const {
    const std::vector<uint32_t> chain = chainOf(ownerOf(drawnId));
    if (chain.empty()) return 0;
    for (size_t k = 1; k < chain.size(); ++k)
        if (isSelected(chain[k])) return chain[k - 1];
    return isSelected(chain.front()) ? chain.front() : chain.back();
}

bool SceneBuilder::drawnSelected(uint32_t drawnId) const {
    const std::vector<uint32_t> chain = chainOf(ownerOf(drawnId));
    return std::any_of(chain.begin(), chain.end(), [this](uint32_t id) { return isSelected(id); });
}

// ---------------------------------------------------------------------------
// Copying and removing whole subtrees
// ---------------------------------------------------------------------------
uint32_t SceneBuilder::copySubtree(uint32_t id, bool instance, uint32_t parent) {
    const uint32_t copy = newId();
    if (const Entity* e = entityById(id)) {
        Entity c = instance ? *e : resolveInstance(graph_, *e); // a copy of an instance is a shape of its own
        c.id = copy;
        c.parent = parent;
        c.locked = false;
        c.instanceOf = instance ? masterOf(id) : 0;
        graph_.entities.push_back(c);
    } else if (const Group* g = groupById(id)) {
        Group c = *g;
        c.id = copy;
        c.parent = parent;
        c.locked = false;
        const std::vector<uint32_t> kids = childrenOf(id); // before the copy joins the graph
        graph_.groups.push_back(c);
        for (uint32_t kid : kids) copySubtree(kid, instance, copy);
    } else if (const ArrayObject* a = arrayById(id)) {
        ArrayObject c = *a; // the copy shares the template
        c.id = copy;
        c.parent = parent;
        c.locked = false;
        graph_.arrays.push_back(c);
    } else if (const Light* l = lightById(id)) {
        Light c = *l;
        c.id = copy;
        c.parent = parent;
        c.locked = false;
        graph_.lights.push_back(c);
    } else if (const Camera* cam = cameraById(id)) {
        Camera c = *cam;
        c.id = copy;
        c.parent = parent;
        c.locked = false;
        c.active = false; // one active camera: the original stays it
        graph_.cameras.push_back(c);
    }
    return copy;
}

// The objects go with everything under them, and an array whose template goes. An instance whose
// master goes keeps what it looked like: it becomes an ordinary shape.
void SceneBuilder::removeObjects(const std::vector<uint32_t>& ids) {
    std::set<uint32_t> dead;
    auto gone = [&](uint32_t id) {
        const std::vector<uint32_t> chain = chainOf(id);
        return std::any_of(chain.begin(), chain.end(), [&](uint32_t x) { return contains(ids, x); });
    };
    for (const Entity& e : graph_.entities)
        if (gone(e.id)) dead.insert(e.id);
    for (const Group& g : graph_.groups)
        if (gone(g.id)) dead.insert(g.id);
    for (const ArrayObject& a : graph_.arrays)
        if (gone(a.id) || dead.count(a.templateId)) dead.insert(a.id);
    for (const Light& l : graph_.lights)
        if (gone(l.id)) dead.insert(l.id);
    for (const Camera& c : graph_.cameras)
        if (gone(c.id)) dead.insert(c.id);
    for (Entity& e : graph_.entities) {
        if (dead.count(e.id) || e.instanceOf == 0) continue;
        const uint32_t master = masterOf(e.id);
        if (!dead.count(master)) continue;
        e = resolveInstance(graph_, e);
        e.instanceOf = 0;
    }
    auto isDead = [&](const SceneObject& o) { return dead.count(o.id) > 0; };
    graph_.entities.erase(std::remove_if(graph_.entities.begin(), graph_.entities.end(), isDead), graph_.entities.end());
    graph_.groups.erase(std::remove_if(graph_.groups.begin(), graph_.groups.end(), isDead), graph_.groups.end());
    graph_.arrays.erase(std::remove_if(graph_.arrays.begin(), graph_.arrays.end(), isDead), graph_.arrays.end());
    graph_.lights.erase(std::remove_if(graph_.lights.begin(), graph_.lights.end(), isDead), graph_.lights.end());
    graph_.cameras.erase(std::remove_if(graph_.cameras.begin(), graph_.cameras.end(), isDead), graph_.cameras.end());
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------
// The group stands at the centre of what it holds; the members keep their places in the world, their
// poses now relative to the group. Members of one parent group stay inside it.
void SceneBuilder::groupSelected() {
    const std::vector<uint32_t> roots = selectionRoots();
    if (roots.empty()) return emit statusMessage("Выберите объекты, которые объединить в группу");
    prepareEdit(0);
    remember();
    Group g;
    g.id = newId();
    g.name = QString("Группа %1").arg(graph_.groups.size() + 1).toStdString();
    AABB box;
    for (uint32_t id : roots) box.expand(boundsOf(id));
    const uint32_t parent = objectById(roots.front())->parent;
    const bool oneParent = std::all_of(roots.begin(), roots.end(), [&](uint32_t id) { return objectById(id)->parent == parent; });
    g.parent = oneParent && findObject(graph_, parent) ? parent : 0;
    setWorldPose(graph_, g, box.valid() ? box.center() : worldPositionOf(roots.front()), Quaternion());
    std::vector<std::pair<Vector3, Quaternion>> poses(roots.size());
    for (size_t i = 0; i < roots.size(); ++i) worldPose(graph_, *objectById(roots[i]), poses[i].first, poses[i].second);
    graph_.groups.push_back(g);
    for (size_t i = 0; i < roots.size(); ++i) {
        SceneObject* o = objectById(roots[i]);
        o->parent = g.id;
        setWorldPose(graph_, *o, poses[i].first, poses[i].second);
    }
    refreshList();
    setSelection({g.id}, g.id);
    applyEdit(0);
    emit statusMessage("Группа: щелчок по ней выбирает всю группу, ещё щелчок — объект внутри");
}

void SceneBuilder::ungroupSelected() {
    std::vector<uint32_t> groups;
    for (uint32_t id : selection_)
        if (groupById(id)) groups.push_back(id);
    if (groups.empty()) return emit statusMessage("Выберите группу, чтобы разгруппировать");
    prepareEdit(0);
    remember();
    std::vector<uint32_t> freed;
    for (uint32_t gid : groups) {
        const uint32_t up = groupById(gid)->parent;
        for (uint32_t kid : childrenOf(gid)) {
            SceneObject* o = objectById(kid);
            Vector3 p;
            Quaternion q;
            worldPose(graph_, *o, p, q);
            o->parent = up;
            setWorldPose(graph_, *o, p, q);
            freed.push_back(kid);
        }
        graph_.groups.erase(std::remove_if(graph_.groups.begin(), graph_.groups.end(), [gid](const Group& g) { return g.id == gid; }),
                            graph_.groups.end());
    }
    refreshList();
    setSelection(freed, freed.empty() ? 0 : freed.front());
    applyEdit(0);
}

void SceneBuilder::setGlued(uint32_t groupId, bool glued) {
    Group* g = groupById(groupId);
    if (!g || g->glued == glued) return;
    prepareEdit(0);
    remember();
    groupById(groupId)->glued = glued;
    refreshList();
    fillInspector();
    applyEdit(0);
}

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------
// Each selected shape becomes the hidden template of a row of 5 copies along X, a little more than
// its width apart; the array stands where the shape stood, so copy 0 is the shape itself.
void SceneBuilder::makeArrayOfSelected() {
    std::vector<uint32_t> shapes;
    for (uint32_t id : selectionRoots())
        if (const Entity* e = entityById(id); e && !isHiddenTemplate(*e)) shapes.push_back(id);
    if (shapes.empty()) return emit statusMessage("Массив делается из формы: выберите куб, шар…");
    prepareEdit(0);
    remember();
    std::vector<uint32_t> arrays;
    for (uint32_t id : shapes) {
        const AABB box = boundsOf(id);
        ArrayObject a;
        a.id = newId();
        a.templateId = id;
        a.name = "Массив: " + entityById(id)->name;
        a.parent = entityById(id)->parent;
        a.color = entityById(id)->color;
        a.pattern = ArrayPattern::Line;
        a.step = Vector3((box.valid() ? box.extent().x : 0.2f) + 0.05f, 0.0f, 0.0f);
        a.count[0] = 5;
        setWorldPose(graph_, a, worldPositionOf(id), Quaternion());
        entityById(id)->visible = false; // the template: the array draws it
        graph_.arrays.push_back(a);
        arrays.push_back(a.id);
    }
    refreshList();
    setSelection(arrays, arrays.back());
    applyEdit(0);
    frameObjects();
}

// Every copy becomes a shape of its own where it stands. The template becomes the first copy when
// no other array uses it; else it stays the other array's hidden template.
void SceneBuilder::explodeArray(uint32_t id) {
    const ArrayObject* found = arrayById(id);
    if (!found) return;
    prepareEdit(0);
    remember();
    const ArrayObject array = *found;
    const std::vector<Entity> copies = expandArray(graph_, array);
    const bool shared = std::count_if(graph_.arrays.begin(), graph_.arrays.end(), [&](const ArrayObject& a) { return a.templateId == array.templateId; }) > 1;
    const std::string name = entityById(array.templateId) ? entityById(array.templateId)->name : array.name;
    std::vector<uint32_t> made;
    for (size_t n = 0; n < copies.size(); ++n) {
        Entity c = copies[n]; // world pose, the template's geometry and components
        const Vector3 p = c.position;
        const Quaternion q = objectRotation(c);
        c.parent = array.parent;
        setWorldPose(graph_, c, p, q);
        if (n == 0 && !shared && entityById(array.templateId)) { // the template itself takes the first place
            Entity& t = *entityById(array.templateId);
            static_cast<SceneObject&>(t) = static_cast<const SceneObject&>(c);
            t.id = array.templateId;
            t.name = name;
            made.push_back(t.id);
            continue;
        }
        c.id = newId();
        c.name = name + " " + std::to_string(n + 1);
        graph_.entities.push_back(c);
        made.push_back(c.id);
    }
    graph_.arrays.erase(std::remove_if(graph_.arrays.begin(), graph_.arrays.end(), [id](const ArrayObject& a) { return a.id == id; }),
                        graph_.arrays.end());
    refreshList();
    setSelection(made, made.empty() ? 0 : made.front());
    applyEdit(0);
}

void SceneBuilder::editArray(uint32_t id, const std::function<void(ArrayObject&)>& change) {
    if (!arrayById(id)) return;
    prepareEdit(0);
    remember(true);
    change(*arrayById(id));
    refreshList();
    if (id == selectedId_) fillInspector();
    applyEdit(0);
}

// ---------------------------------------------------------------------------
// Instances
// ---------------------------------------------------------------------------
void SceneBuilder::unlinkInstance(uint32_t id) {
    const Entity* e = entityById(id);
    if (!e || e->instanceOf == 0) return;
    const Entity own = resolveInstance(graph_, *e);
    editEntity(id, [own](Entity& x) {
        x = own;
        x.instanceOf = 0;
    });
}

// ---------------------------------------------------------------------------
// The selection helpers of the menus
// ---------------------------------------------------------------------------
// "Такие же": for a shape, the shapes of the same form with the same components, and every instance
// of the same master; for a group every group, for an array every array.
void SceneBuilder::selectSimilar() {
    const SceneObject* active = selectedObject();
    if (!active) return;
    std::vector<uint32_t> ids;
    if (groupById(active->id)) {
        for (const Group& g : graph_.groups)
            if (g.visible && !g.locked) ids.push_back(g.id);
    } else if (arrayById(active->id)) {
        for (const ArrayObject& a : graph_.arrays)
            if (a.visible && !a.locked) ids.push_back(a.id);
    } else {
        const Entity like = resolveInstance(graph_, *entityById(active->id));
        const uint32_t master = masterOf(active->id);
        for (const Entity& e : graph_.entities) {
            if (!e.visible || e.locked) continue;
            const Entity r = resolveInstance(graph_, e);
            if (masterOf(e.id) == master || (r.shape == like.shape && roleBits(r) == roleBits(like))) ids.push_back(e.id);
        }
    }
    setSelection(ids, active->id);
}

void SceneBuilder::selectWithRole(RoleIcon role) {
    std::vector<uint32_t> ids;
    for (const Entity& e : graph_.entities)
        if (e.visible && !e.locked && roleEnabled(resolveInstance(graph_, e), role)) ids.push_back(e.id);
    if (ids.empty()) emit statusMessage("Ни у одного объекта нет компонента «" + roleTitle(role) + "»");
    setSelection(ids, selectedId_);
}

void SceneBuilder::invertSelection() {
    std::vector<uint32_t> ids;
    for (uint32_t id : topLevelObjects())
        if (!drawnSelected(id)) ids.push_back(id);
    setSelection(ids, 0);
}
