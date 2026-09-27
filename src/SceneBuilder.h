#pragma once
// The scene builder ("Конструктор"): a scene made without code, in two modes, as in Unity, Unreal or
// Blender.
//   ПРАВКА (edit, the default) - a plain 3D editor. The big buttons create GEOMETRY ONLY: a shape
//     with no role that stands where it was put; nothing moves, nothing falls, nothing is simulated.
//     Objects are selected with the mouse (again on the same spot: the one behind), moved, turned
//     and scaled with the gizmo or the keyboard, and given roles - rigid, soft, liquid or cloth, and
//     also a magnet, a smoke trail, something that burns, a hot spot - like King Midas turning what
//     he touches. In edit mode a role only marks the object.
//   ▶ ПУСК (play) - the simulation is built from the graph once and runs; ⏸ pauses it; ■ СТОП throws
//     it away and shows the scene exactly as it was before Play (the curtain whole again, the cube
//     back in place). As in Unity, edits made while playing are not kept after Stop.
//
// The scene has three layers: SOURCES (geometry: never destroyed by physics), OBJECTS (a source with
// a pose and roles - what the gizmo and the inspector edit) and META-OBJECTS (the physics a role makes
// when the scene plays). While the scene plays the inspector stays live: every edit goes through
// applyEditDuringPlay(), which compares the graph with what the simulation was last given and has
// the simulation rebuild only the objects that changed (GraphScene::rebuildEntity: a falling cube
// turned from water into jelly goes on falling as jelly). Only what the world is made of (gravity,
// the box, the gas) needs the scene rebuilt: then the builder stops and says why. On pause the gizmo
// works on the object where it is now.
//
// The builder owns an rf::SceneGraph (saved as *.rfscene) and shows two tabs, as Unity's hierarchy
// and inspector: "Сцена" (what is there, the eye and the lock of each thing, and the world) and
// "Объект" (the selected thing, top to bottom: a quick palette of components, the "Объект" card
// every thing has - name, visibility, lock, colour, position, rotation, size -, the "Геометрия" card
// - what it looks like, which physics never changes -, then only the COMPONENTS it has, each a card
// with an ✕, and "+ Добавить компонент"). The collider is a component of its own: alone it makes a
// fixed obstacle, with "Твёрдое тело" a moving body; adding "Твёрдое тело" adds a collider "Авто",
// as Unity's primitives come with theirs. The collider is drawn as a thin green wireframe over the
// geometry (Houdini's collision guide): always for the selected object, for all with "Коллайдеры".
//
// Many objects at once (docs/controls.md): the selection may hold several objects, and a thing in
// the scene is an rf::SceneObject - a shape (Entity), a group (Ctrl+G: its members move with it) or
// an array (one object standing for a row, a grid or a circle of copies of a template shape). An
// instance shares the geometry and components of its master: editing one edits all. Shift held
// when a gizmo drag starts clones the selection (SceneBuilderClone.cpp); the inspector edits every
// selected object at once, a number that differs between them showing "—" (SceneBuilderMulti.cpp).
//
// Lights and cameras (SceneBuilderLights.cpp) are objects too, with no geometry and no physics: the
// toolbar's "Свет ▾" (Солнце, Лампа, Прожектор) and "Камера" make them, the viewport draws them as
// wireframes (SceneMarkers.h) that the mouse picks and the gizmo moves and turns (a scale does
// nothing to them), the "Свет" and "Камера" cards edit them, and "Смотреть через камеру" makes the
// view that camera: moving the view then moves the camera (Blender's "lock camera to view").
#include "ColliderGuides.h"
#include "EditView.h"
#include "Gizmo.h"
#include "Icons.h"
#include "SceneMarkers.h"
#include "SimController.h"
#include "scene/SceneGraph.h"

#include <QElapsedTimer>
#include <QObject>
#include <QRectF>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ArrayPanel;
class ColliderPanel;
class ComponentCard;
class GroupPanel;
class InlineBanner;
class ObjectInspector;
class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;
class RoleBar;
class Vec3Row;

class SceneBuilder : public QObject {
    Q_OBJECT
public:
    enum class Mode { Edit, Playing, Paused };
    // What a Shift + gizmo drag makes (the "Клонировать" popover): independent copies, instances of
    // the originals (one geometry, one set of components), or one array per original.
    enum class CloneKind { Copy, Instance, Array };

    explicit SceneBuilder(SimController* ctrl, QObject* parent = nullptr);

    QWidget* sceneListPanel() const { return listPanel_; }
    QWidget* inspectorPanel() const { return inspectorPanel_; }
    const std::vector<QAction*>& createActions() const { return createActions_; } // Куб, Сфера ...
    QAction* undoAction() const { return undoAct_; }
    QAction* redoAction() const { return redoAct_; }
    QAction* deselectAction() const { return deselectAct_; }

    void newScene();                         // a floor and nothing else, in edit mode
    bool openFile(const QString& path, QString& error);
    bool saveFile(const QString& path, QString& error) const;
    bool importModel(const QString& path, QString& error); // an OBJ / STL model as geometry
    const rf::SceneGraph& graph() const { return graph_; }
    static std::string sceneName() { return "Конструктор"; } // the simulation's name of the builder's scene

    // Play / pause / stop / one step. A ready-made scene of the SDK (MainWindow loads it) makes the
    // builder step aside: play and pause work on it, stop takes it back to its start.
    Mode mode() const { return mode_; }
    bool editing() const { return mode_ == Mode::Edit && !sample_; }
    bool gizmoAllowed() const { return !sample_ && mode_ != Mode::Playing; } // edit mode, or paused
    int bodyOfEntity(uint32_t id) const;   // the first drawn body of an object (-1: none)
    uint32_t entityOfBody(int body) const; // the drawn id of a body: an entity, an array's copy... (0: none)
    // Play mode: the simulation's newest picture (the live pose of the selected object on pause).
    void onSimulationSnapshot(const rf::RenderSnapshot& s);
    bool showsSample() const { return sample_; }
    void play();
    void pause();
    void stop();
    void step();
    void showSample();
    // K in play or on pause (SceneBuilderKeep.cpp, Unreal's "Keep Simulation Changes"): the live poses
    // of the selected objects (all, if none is selected) stay after Stop; one undo step after Stop.
    void keepSimulationPoses();

    // Edit mode, from the viewport: what the mouse ray passes through, clicks, hover, the gizmo.
    // A hit's id is what a click there selects (clickTarget): the topmost group of the thing first;
    // with that group selected, the next level down, and so on to the thing itself.
    std::vector<PickHit> pickAll(const Ray& ray);
    uint32_t clickTarget(uint32_t drawnId) const;
    bool drawnSelected(uint32_t drawnId) const; // the thing, or a group or array it belongs to, is selected
    void onEntityClicked(uint32_t id);
    void onEntityHovered(uint32_t id);
    void onGizmoStarted();
    void onGizmoMoved(const GizmoPose& pose);
    void onGizmoFinished();
    void onGizmoCancelled();
    void onCloneDragStarted(); // Shift was held when the drag began: the drag moves copies (SceneBuilderClone.cpp)
    void selectBody(int body); // play mode: a click on a simulated body selects its object

    // What the context menu does to the selected object.
    void removeSelected();
    void duplicateSelected();
    void toggleRole(RoleIcon role);
    void toggleVisible(uint32_t id);
    void toggleLocked(uint32_t id);
    void frameSelected(); // the camera shows the selected objects (F); nothing selected: all
    void frameAll() { frameObjects(); } // Home
    // The collider wireframes of all objects (the toolbar's "Коллайдеры"); the selected one always.
    void setShowAllColliders(bool on);
    // The first-minute hint and the like: one sentence and a button that fixes the cause.
    void showBanner(const QString& text, const QString& buttonText = {}, std::function<void()> fix = {});

    // The selection: one object is active (the inspector and the gizmo show it); Shift / Ctrl + click
    // and the selection box add more. Delete, duplicate, hide and the gizmo act on all of them
    // (SceneBuilderSelection.cpp).
    const std::vector<uint32_t>& selection() const { return selection_; }
    bool isSelected(uint32_t id) const;
    void toggleSelected(uint32_t id); // Shift / Ctrl + click
    void selectAll();                 // Ctrl+A: every visible, unlocked object
    void selectInRect(const QRectF& rect, const GizmoView& view, bool add, bool remove); // the box
    void hideSelected();              // H
    void unhideAll();                 // Alt+H
    void selectSimilar();             // "Выбрать все такие же": same shape and components, or the same master
    void selectWithRole(RoleIcon role); // "Выбрать все с компонентом"
    void invertSelection();           // "Инвертировать выбор"

    // Many objects at once (SceneBuilderObjects.cpp). Each is one undo step.
    void groupSelected();             // Ctrl+G: a group at the centre of the selection, poses kept in the world
    void ungroupSelected();           // Ctrl+Shift+G
    void setGlued(uint32_t groupId, bool glued); // "Склеить в одно тело": the members become one rigid body
    void makeArrayOfSelected();       // "Сделать массивом…": each selected shape becomes the template of a row of 5
    void explodeArray(uint32_t id);   // "Разобрать на объекты": every copy an object of its own
    void editArray(uint32_t id, const std::function<void(rf::ArrayObject&)>& change);
    void unlinkInstance(uint32_t id); // "Отвязать": the instance gets its own geometry and components
    const rf::SceneObject* object(uint32_t id) const { return rf::findObject(graph_, id); }
    QString objectTitle(uint32_t id) const; // "Куб 1", "Группа 1", "Массив: 5 × Куб 1"
    uint32_t masterOf(uint32_t entityId) const; // the entity whose geometry an instance shows (itself if none)

    // Shift + gizmo = clone (SceneBuilderClone.cpp). After the drag the copy stands where it was
    // dragged; the popover asks how many and of what kind, and then the originals stay where they
    // were and the copies repeat the same step: moved d -> d, 2d, 3d...; turned a -> a, 2a... about
    // the gizmo's centre; scaled s -> s, s², s³... The whole clone is one undo step.
    bool clonePending() const { return clonePending_; }
    bool cloneAllows(CloneKind kind) const; // an array or an instance cannot repeat a scale
    void finishClone(int copies, CloneKind kind);
    void cancelClone();

    // Lights and cameras (SceneBuilderLights.cpp). A new light or camera is one undo step.
    const std::vector<QAction*>& lightActions() const { return lightActions_; } // Солнце, Лампа, Прожектор
    QAction* cameraAction() const { return cameraAct_; }
    void addLight(rf::LightKind kind);  // the sun high, shining down at a slant; a lamp 1.5 m over the selection
    void addCamera();                   // at the editor's eye, looking at the scene's centre
    void setViewEye(std::function<rf::Vector3()> eye) { viewEye_ = std::move(eye); }
    void editLight(uint32_t id, const std::function<void(rf::Light&)>& change);
    void editCamera(uint32_t id, const std::function<void(rf::Camera&)>& change);
    // The view through a camera: it becomes the scene's active camera and the viewport shows what it
    // sees; 0 gives the editor's own view back. Not an undo step (the view is no edit of the scene).
    void lookThrough(uint32_t cameraId);
    void lookThroughActiveCamera(); // the active camera, else the first one (the command line's --camera)
    uint32_t lookingThrough() const { return throughId_; }
    // Looking through a camera the view moved: the camera goes where the view is now.
    void onViewMovedThroughCamera(const rf::Vector3& eye, const rf::Vector3& forward, const rf::Vector3& up);

    // Scripted edits (the self-test, later a script console): one undo step each.
    void select(uint32_t id) { setSelected(id); }
    void selectMany(const std::vector<uint32_t>& ids) { setSelection(ids, ids.empty() ? 0 : ids.back()); }
    uint32_t selectedId() const { return selectedId_; }
    void editEntity(uint32_t id, const std::function<void(rf::Entity&)>& change);

signals:
    void statusMessage(const QString& text);               // "Включён газ: дым нуждается в воздухе"
    void simulationKept(int objects);                      // K took the live poses of this many objects
    void modeChanged();                                    // edit / playing / paused (or a sample)
    void editSnapshot(std::shared_ptr<const rf::RenderSnapshot> s); // edit mode: what to draw
    void highlightBodies(const std::vector<int>& bodies);  // the bodies of the selected thing
    void hoverBodies(const std::vector<int>& bodies);      // the body under the mouse
    void ghostBodies(const std::vector<int>& bodies);      // geometry without a role (edit mode)
    void unpickableBodies(const std::vector<char>& flags); // per body: locked, the mouse passes through
    void gizmoTarget(bool visible, rf::Vector3 position, rf::Quaternion rotation);
    void frameRequested(const rf::AABB& box);              // show these objects (after a load or an add)
    void sceneryBodies(std::vector<rf::RenderSnapshot::Body> bodies); // play: geometry without a role, drawn as is
    void selectionChanged(uint32_t id);                    // the "Объект" tab shows it (0: nothing selected)
    void colliderGuides(std::vector<float> lines);         // green wireframes: pairs of points, x y z 0 each
    void firstAction();                                    // the user did something: the first-start hint goes
    void sceneMarkers(std::vector<SceneMarker> markers);   // the lights and cameras to draw
    // The view through a camera (on) or the editor's own view (off).
    void cameraView(bool on, rf::Vector3 eye, rf::Vector3 forward, rf::Vector3 up, float fovDeg, float nearClip, float farClip);

private:
    // Building the panels
    void buildActions();
    void buildSceneList();
    void buildWorldSection(QVBoxLayout* col);
    void buildInspector();
    void buildHeader(QVBoxLayout* col);
    void buildObjectCard(QVBoxLayout* col);        // what every thing has, and the size
    void buildGeometryCard(QVBoxLayout* col);      // what it looks like
    void buildMaterialCards(QVBoxLayout* col);     // rigid (+ the no-collider warning), soft, liquid, cloth
    void buildColliderCard(QVBoxLayout* col);
    void buildBehaviourCards(QVBoxLayout* col);    // magnet, emitter, burns, heat
    void buildAddComponent(QVBoxLayout* col);      // "+ Добавить компонент" and its menu
    void buildManyCards(QVBoxLayout* col);         // "Группа", "Массив" and the instance's link
    void fillAddMenu();                            // the components the object does not have yet
    ComponentCard* componentCard(QVBoxLayout* col, RoleIcon role, QFormLayout*& form);
    QDoubleSpinBox* numberField(QFormLayout* f, const QString& label, double min, double max, double step, int decimals);
    QCheckBox* checkField(QFormLayout* f, const QString& text);
    Vec3Row* vectorField(QFormLayout* f, const QString& label, double min, double max, double step, int decimals);

    // Editing
    void prepareEdit(uint32_t entityId);        // every edit calls this first
    void applyEdit(uint32_t entityId);          // ... and this after: edit mode redraws, play mode updates
    void applyEditDuringPlay(uint32_t entityId); // an edit while the simulation runs (see the top)
    void onPlayEditApplied(bool ok, const std::vector<uint32_t>& bodyEntity,
                           const std::vector<std::pair<uint32_t, std::pair<rf::Vector3, rf::Vector3>>>& poses);
    void reloadNeeded(const QString& why);
    void addEntity(rf::ShapeKind shape);
    rf::Vector3 freeSpot() const;               // a place on the floor no other object stands on
    void switchOnWhatRoleNeeds(rf::Entity& e, RoleIcon role);
    void keepComponentsConsistent(rf::Entity& e, RoleIcon role, bool on); // the collider goes with a rigid body
    void onObjectEdited();
    void onDetailsEdited();
    void onArrayEdited();
    void onWorldEdited();
    void onListClicked(QTreeWidgetItem* item, int column);
    void setSelected(uint32_t id);                              // just this one (0: nothing)
    void setSelection(std::vector<uint32_t> ids, uint32_t active); // every change of the selection goes here
    void syncListSelection();                                   // the scene list shows the selection

    // Graph <-> widgets
    void fillInspector();
    void fillManyCards(const rf::SceneObject& o); // the group, array and instance cards of the active object
    void fillDetails(const rf::Entity& e);
    void readDetails(rf::Entity& e) const;
    void fillWorld();
    void refreshList();
    void refreshHeader();
    void refreshSections();
    void refreshBanner();       // what does not work for the selected object, and the fix
    void showTransform(const rf::SceneObject& o); // the gizmo moved it: the numbers follow, not an edit

    // Several selected (SceneBuilderMulti.cpp): the widgets show the active object and "—" where
    // the selected differ; an edit is what differs from what the widgets showed, and goes to all.
    rf::Entity readWidgets() const;        // what the widgets show now, as an entity
    void markMixed();                      // "—" in every field whose value differs between the selected
    void applyWidgetEdit(bool details);    // the changed fields into every selected object
    std::vector<rf::Entity*> selectedEntities(); // the selected shapes (instances: their own objects)
    std::vector<RoleIcon> commonRoles() const;   // the components every selected shape has

    // Lights and cameras (SceneBuilderLights.cpp)
    void buildLightActions();
    void buildLightCards(QVBoxLayout* col);
    QDoubleSpinBox* lightNumber(QFormLayout* f, const QString& label, double min, double max, double step, int decimals, bool camera);
    void fillLightCards(const rf::SceneObject& o);
    void onLightEdited();
    void onCameraEdited();
    void addObjectDone(uint32_t id);            // select it, list it, show it
    rf::Vector3 spawnTarget();                  // the selection's place, else the centre of the shapes
    void refreshLightsAndCameras();             // the wireframes and the view through a camera
    void refreshMarkers();
    void emitCameraView();
    std::vector<PickHit> pickMarkers(const Ray& ray) const;
    QIcon kindIcon(uint32_t id, int size) const; // a light's or a camera's icon (null: neither)
    rf::Light* lightById(uint32_t id);
    rf::Camera* cameraById(uint32_t id);

    // History: every change can be undone (Ctrl+Z) and redone (Ctrl+Shift+Z)
    void remember(bool mergeWithLast = false);
    void undo();
    void redo();
    void restore(rf::SceneGraph g);
    void updateHistoryActions();

    // What the viewport shows
    void setMode(Mode m);
    void returnToEdit(bool keepEdits); // throw the simulation away, show the scene (as before Play unless kept)
    void loadIntoSimulation();  // build the simulation from the graph (play, or a step from edit)
    void refreshView();         // edit mode: draw the graph as it is now
    void refreshScenery();      // play mode: the shapes that get no body (the simulation does not draw them)
    std::string displayName() const;
    void onBodiesMapped(const std::vector<uint32_t>& bodyEntity);
    void frameObjects(); // asks the viewport to show the things of the scene, not the empty world
    void sendViewportFlags();
    void updateGizmoTarget();
    // The collider wireframes: from the graph in edit mode, from the bodies while the scene plays.
    void refreshColliderGuides();
    void playColliderGuides(const rf::RenderSnapshot& s);
    // The collider of an entity as the simulation will build it, kept until its shape or size changes.
    const rf::EntityCollider& cachedCollider(const rf::Entity& e);

    // Lookups (SceneBuilderObjects.cpp for the objects)
    int indexOf(uint32_t id) const;
    rf::Entity* selectedEntity();
    rf::SceneObject* selectedObject();
    rf::SceneObject* objectById(uint32_t id);
    rf::Entity* entityById(uint32_t id);
    rf::Group* groupById(uint32_t id);
    rf::ArrayObject* arrayById(uint32_t id);
    uint32_t ownerOf(uint32_t drawnId) const;        // an array's copy -> the array; anything else itself
    std::vector<uint32_t> chainOf(uint32_t id) const; // the object, its group, that group's group... to the top
    bool isInside(uint32_t id, uint32_t ancestor) const; // id is the ancestor or somewhere under it
    std::vector<uint32_t> childrenOf(uint32_t id) const;
    std::vector<uint32_t> selectionRoots() const;   // selected, unlocked, and no selected group above
    std::vector<uint32_t> topLevelObjects() const;  // visible, unlocked things not in a group
    bool isHiddenTemplate(const rf::Entity& e) const; // the hidden shape an array copies
    rf::AABB boundsOf(uint32_t id);                 // the world box of what an object draws
    rf::Vector3 worldPositionOf(uint32_t id) const;
    uint32_t copySubtree(uint32_t id, bool instance, uint32_t parent); // with new ids; returns the copy's id
    void removeObjects(const std::vector<uint32_t>& ids); // with everything under them
    void scaleFrom(const rf::SceneGraph& start, uint32_t id, const rf::Vector3& factors); // sizes and spacing from start
    // A Shift + gizmo drag's copies (SceneBuilderClone.cpp): copy k of an original, moved and turned
    // k times by the drag's step (from0 -> to), scaled by the factors to the k-th power.
    struct CloneStep {
        rf::Vector3 from, to;
        rf::Quaternion fromRotation, toRotation;
        rf::Vector3 scale{1.0f};
        bool turns = false;
    };
    uint32_t placeClone(uint32_t original, const CloneStep& step, int k, bool instance);
    uint32_t cloneAsArray(uint32_t original, const CloneStep& step, int copies);
    uint32_t newId();
    void pruneSelection(); // drops what is no longer in the graph
    void adoptIds(); // give ids to entities that came without one (old files); nextId_ past all

    SimController* ctrl_;
    rf::SceneGraph graph_;
    rf::SceneGraph playBackup_;    // the graph as it was when Play was pressed: Stop brings it back
    rf::SceneGraph simGraph_;      // what the simulation was last given (edits during play are diffed with it)
    bool hasPlayBackup_ = false;
    // K (SceneBuilderKeep.cpp): a live pose read on the simulation's thread, and the scene as it was
    // before the first K of this play - Stop puts it into the history as one undo step.
    struct KeptPose {
        uint32_t id = 0;
        rf::Vector3 position{0.0f};
        rf::Quaternion rotation;
        bool turned = false; // false: a particle shape, whose angles stay
    };
    void applyKeptPoses(const std::vector<KeptPose>& poses);
    rf::SceneGraph keptBefore_;
    bool hasKeptBefore_ = false;
    size_t undoAtPlay_ = 0;        // the history's length at Play: steps made while playing go on Stop
    rf::Vector3 liveTarget_{0.0f}; // on pause: where the selected object's body is now
    bool liveTargetValid_ = false;
    Mode mode_ = Mode::Edit;
    bool sample_ = false;          // a ready-made scene of the SDK is shown instead of the builder's
    EditView editView_;
    uint32_t nextId_ = 1;          // ids are never reused within a session
    uint32_t selectedId_ = 0;      // 0: nothing selected
    uint32_t hoverId_ = 0;         // the object under the mouse (edit mode)
    QString fileName_;
    bool filling_ = false;         // widgets are being filled from the graph: not an edit
    std::vector<uint32_t> bodyEntity_; // drawn body -> id of its entity (edit view, or the simulation)

    std::vector<rf::SceneGraph> undo_, redo_;
    QElapsedTimer lastRemember_;

    ColliderGuides guides_;
    bool showAllColliders_ = false;
    // Edit mode: the collider of each entity, rebuilt only when its geometry or collider changes.
    std::map<uint32_t, std::pair<std::string, rf::EntityCollider>> editColliders_;

    // The gizmo: the active object it drags, every dragged object's world pose when the drag began,
    // and the whole graph then (Esc puts it back; a scale is always from the start sizes).
    struct GizmoStart {
        rf::Vector3 position;
        rf::Quaternion rotation;
    };
    uint32_t gizmoEntity_ = 0;
    std::map<uint32_t, GizmoStart> gizmoStarts_;
    GizmoStart gizmoFrom_;         // the active object's world pose when the drag began (the gizmo's own start)
    rf::SceneGraph gizmoGraph_;
    GizmoPose lastPose_;
    // A clone drag: each original and the copy of it the gizmo moves; after the drag, the popover.
    std::vector<std::pair<uint32_t, uint32_t>> cloneOf_;
    bool cloneDrag_ = false, clonePending_ = false;
    std::vector<uint32_t> selection_; // every selected object, the active one (selectedId_) among them
    rf::Entity shown_;                // the widgets as they were filled: an edit is what differs from it

    std::vector<QAction*> createActions_;
    std::vector<QAction*> lightActions_;
    QAction* cameraAct_ = nullptr;
    std::function<rf::Vector3()> viewEye_; // where the editor's view is (a new camera stands there)
    uint32_t throughId_ = 0;               // the camera the view looks through (0: the editor's view)
    QAction *undoAct_ = nullptr, *redoAct_ = nullptr, *deselectAct_ = nullptr;

    // Scene list panel
    QWidget* listPanel_ = nullptr;
    QTreeWidget* list_ = nullptr;
    Vec3Row *gravity_ = nullptr, *worldSize_ = nullptr;
    QCheckBox *gas_ = nullptr, *plasma_ = nullptr;

    // Inspector panel
    QWidget* inspectorPanel_ = nullptr;
    QWidget* inspectorBody_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QLabel* playHint_ = nullptr;     // "Остановите ■, чтобы править"
    InlineBanner* banner_ = nullptr; // why something does not work, and the fix
    bool stickyBanner_ = false;      // a message about the whole scene (a restart needed): kept until ▶
    QLabel* headerIcon_ = nullptr;
    QLabel* headerName_ = nullptr;
    QWidget* headerChips_ = nullptr; // small role icons next to the name
    RoleBar* roleBar_ = nullptr;
    ObjectInspector* object_ = nullptr;
    QComboBox* shape_ = nullptr;
    QLabel* modelFile_ = nullptr;
    Vec3Row* size_ = nullptr;
    ComponentCard* cards_[int(RoleIcon::Count)] = {};
    QWidget* sizeRow_ = nullptr;     // the size: a shape's only
    QWidget* shapePart_ = nullptr;   // "Геометрия", the components and "+ Добавить компонент"
    InlineBanner* instanceLink_ = nullptr; // "Экземпляр: связан с «Куб 1»" [Отвязать]
    ComponentCard *groupCard_ = nullptr, *arrayCard_ = nullptr;
    GroupPanel* groupPanel_ = nullptr;
    ArrayPanel* arrayPanel_ = nullptr;
    QPushButton* addComponent_ = nullptr;
    QMenu* addMenu_ = nullptr;
    InlineBanner* noCollider_ = nullptr; // in the rigid card: a body without a collider falls through
    ColliderPanel* collider_ = nullptr;
    QDoubleSpinBox *rigidDensity_ = nullptr, *friction_ = nullptr, *restitution_ = nullptr;
    QCheckBox* fixed_ = nullptr;
    Vec3Row *velocity_ = nullptr, *spin_ = nullptr;
    QDoubleSpinBox *softDensity_ = nullptr, *stiffness_ = nullptr;
    QDoubleSpinBox *clothDensity_ = nullptr, *bend_ = nullptr;
    QCheckBox* tearable_ = nullptr;
    QCheckBox* pinned_[5] = {};    // -x, +x, -z, +z edges and the rod (bits 1, 2, 4, 8, 16)
    Vec3Row* moment_ = nullptr;
    QDoubleSpinBox *emitSmoke_ = nullptr, *emitTemperature_ = nullptr, *emitLiquid_ = nullptr;
    Vec3Row* emitVelocity_ = nullptr;
    QDoubleSpinBox *heatTemperature_ = nullptr, *heatSmoke_ = nullptr;
    // The "Свет" and "Камера" cards; what they showed (an edit is what differs, for every selected one)
    ComponentCard *lightCard_ = nullptr, *cameraCard_ = nullptr;
    QFormLayout *lightForm_ = nullptr, *cameraForm_ = nullptr;
    QToolButton* lightKind_[3] = {};
    QDoubleSpinBox *lightIntensity_ = nullptr, *lightRange_ = nullptr, *lightCone_ = nullptr, *lightSoftness_ = nullptr;
    QCheckBox* lightShadows_ = nullptr;
    QDoubleSpinBox *cameraFov_ = nullptr, *cameraNear_ = nullptr, *cameraFar_ = nullptr;
    QPushButton* throughButton_ = nullptr;
    rf::Light lightShown_;
    rf::Camera cameraShown_;
};

// The names of the shapes, in the order of rf::ShapeKind: "Куб", "Сфера" ... "Модель".
const char* shapeName(rf::ShapeKind k);
// The names and icons of the kinds of light: "Солнце", "Лампа", "Прожектор".
const char* lightName(rf::LightKind k);
SceneIcon lightIcon(rf::LightKind k);
