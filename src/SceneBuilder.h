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
#include "ColliderGuides.h"
#include "EditView.h"
#include "Gizmo.h"
#include "Icons.h"
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

class ColliderPanel;
class ComponentCard;
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
    int bodyOfEntity(uint32_t id) const;   // the first drawn body of an entity (-1: none)
    uint32_t entityOfBody(int body) const; // 0: none
    // Play mode: the simulation's newest picture (the live pose of the selected object on pause).
    void onSimulationSnapshot(const rf::RenderSnapshot& s);
    bool showsSample() const { return sample_; }
    void play();
    void pause();
    void stop();
    void step();
    void showSample();

    // Edit mode, from the viewport: what the mouse ray passes through, clicks, hover, the gizmo.
    std::vector<PickHit> pickAll(const Ray& ray);
    void onEntityClicked(uint32_t id);
    void onEntityHovered(uint32_t id);
    void onGizmoStarted();
    void onGizmoMoved(const GizmoPose& pose);
    void onGizmoFinished();
    void onGizmoCancelled();
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

    // Scripted edits (the self-test, later a script console): one undo step each.
    void select(uint32_t id) { setSelected(id); }
    uint32_t selectedId() const { return selectedId_; }
    void editEntity(uint32_t id, const std::function<void(rf::Entity&)>& change);

signals:
    void statusMessage(const QString& text);               // "Включён газ: дым нуждается в воздухе"
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
    void onWorldEdited();
    void onListClicked(QTreeWidgetItem* item, int column);
    void setSelected(uint32_t id);                              // just this one (0: nothing)
    void setSelection(std::vector<uint32_t> ids, uint32_t active); // every change of the selection goes here
    void syncListSelection();                                   // the scene list shows the selection

    // Graph <-> widgets
    void fillInspector();
    void fillDetails(const rf::Entity& e);
    void readDetails(rf::Entity& e) const;
    void fillWorld();
    void refreshList();
    void refreshHeader();
    void refreshSections();
    void refreshBanner();       // what does not work for the selected object, and the fix
    void showTransform(const rf::Entity& e); // the gizmo moved it: the numbers follow, not an edit

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

    // Lookups
    int indexOf(uint32_t id) const;
    rf::Entity* selectedEntity();
    uint32_t newId();
    void adoptIds(); // give ids to entities that came without one (old files); nextId_ past all

    SimController* ctrl_;
    rf::SceneGraph graph_;
    rf::SceneGraph playBackup_;    // the graph as it was when Play was pressed: Stop brings it back
    rf::SceneGraph simGraph_;      // what the simulation was last given (edits during play are diffed with it)
    bool hasPlayBackup_ = false;
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

    uint32_t gizmoEntity_ = 0;     // the active entity the gizmo is dragging
    std::map<uint32_t, rf::Entity> gizmoStarts_; // every dragged entity as it was when the drag began (Esc puts them back)
    std::vector<uint32_t> selection_; // every selected entity, the active one (selectedId_) among them

    std::vector<QAction*> createActions_;
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
};

// The names of the shapes, in the order of rf::ShapeKind: "Куб", "Сфера" ... "Модель".
const char* shapeName(rf::ShapeKind k);
