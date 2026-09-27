#pragma once
// The scene builder ("Конструктор"): a scene made without code. Big buttons create shapes; the role
// bar turns the selected shape into physics - rigid, soft, liquid or cloth, and also a magnet, a
// smoke trail, something that burns or a hot spot - like King Midas turning what he touches. The
// builder owns an rf::SceneGraph (the scene as data, saved as *.rfscene) and after every change
// sends a copy to the simulation thread as a GraphScene, so what you click is what runs.
//
// It shows two panels: the scene list (what is there, the eye and the lock of each thing, and the
// world) and the inspector (the "Объект" block every thing has, the shape, and only the details of
// the roles that are on). A role switches on what it needs (smoke turns on the gas, cloth hangs
// itself up), every change can be undone, and Shift+drag moves a shape across the floor.
#include "Icons.h"
#include "SimController.h"
#include "scene/SceneGraph.h"

#include <QElapsedTimer>
#include <QObject>

#include <vector>

class CollapsibleSection;
class ObjectInspector;
class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;
class QVBoxLayout;
class QWidget;
class RoleBar;
class Vec3Row;

class SceneBuilder : public QObject {
    Q_OBJECT
public:
    explicit SceneBuilder(SimController* ctrl, QObject* parent = nullptr);

    QWidget* sceneListPanel() const { return listPanel_; }
    QWidget* inspectorPanel() const { return inspectorPanel_; }
    const std::vector<QAction*>& createActions() const { return createActions_; } // Куб, Сфера ...
    QAction* undoAction() const { return undoAct_; }
    QAction* redoAction() const { return redoAct_; }

    void newScene();                         // a floor and nothing else, running
    bool openFile(const QString& path, QString& error);
    bool saveFile(const QString& path, QString& error) const;
    const rf::SceneGraph& graph() const { return graph_; }
    static std::string sceneName() { return "Конструктор"; } // the simulation's name of the builder's scene

    // The viewport's mouse: a click selects the thing it hit; Shift+drag moves it on the floor.
    void selectBody(int body);
    void moveStarted(int body, rf::Vector3 hit);
    void moveDragged(rf::Vector3 point);
    void moveFinished();

signals:
    void statusMessage(const QString& text);               // "Включён газ: дым нуждается в воздухе"
    void wantsRunning();                                   // something was created: let it fall at once
    void highlightBodies(const std::vector<int>& bodies);  // the bodies of the selected thing
    void unpickableBodies(const std::vector<char>& flags); // per body: locked, the mouse passes through
    void frameRequested(const rf::AABB& box);              // show these objects (after a load or an add)

private:
    // Building the panels
    void buildActions();
    void buildSceneList();
    void buildWorldSection(QVBoxLayout* col);
    void buildInspector();
    void buildHeader(QVBoxLayout* col);
    void buildShapeBlock(QVBoxLayout* col);
    void buildMaterialSections(QVBoxLayout* col);  // rigid, soft, liquid, cloth
    void buildBehaviourSections(QVBoxLayout* col); // magnet, smoke, burns, heat
    CollapsibleSection* roleSection(QVBoxLayout* col, RoleIcon role, QFormLayout*& form);
    QDoubleSpinBox* numberField(QFormLayout* f, const QString& label, double min, double max, double step, int decimals);
    QCheckBox* checkField(QFormLayout* f, const QString& text);
    Vec3Row* vectorField(QFormLayout* f, const QString& label, double min, double max, double step, int decimals);

    // Editing
    void addEntity(rf::ShapeKind shape);
    void removeSelected();
    void duplicateSelected();
    void toggleRole(RoleIcon role);
    void switchOnWhatRoleNeeds(rf::Entity& e, RoleIcon role);
    void onObjectEdited();
    void onDetailsEdited();
    void onWorldEdited();
    void onListClicked(QTreeWidgetItem* item, int column);
    void setSelected(uint32_t id);

    // Graph <-> widgets
    void fillInspector();
    void fillDetails(const rf::Entity& e);
    void readDetails(rf::Entity& e) const;
    void fillWorld();
    void refreshList();
    void refreshHeader();
    void refreshSections();

    // History: every change can be undone (Ctrl+Z) and redone (Ctrl+Shift+Z)
    void remember(bool mergeWithLast = false);
    void undo();
    void redo();
    void restore(rf::SceneGraph g);
    void updateHistoryActions();

    // The simulation
    void scheduleReload();
    void reload();
    void onBodiesMapped(const std::vector<uint32_t>& bodyEntity);
    void frameObjects(); // asks the viewport to show the things of the scene, not the empty world
    void sendViewportFlags();

    // Lookups
    int indexOf(uint32_t id) const;
    rf::Entity* selectedEntity();
    uint32_t newId();
    void adoptIds(); // give ids to entities that came without one (old files); nextId_ past all

    SimController* ctrl_;
    rf::SceneGraph graph_;
    uint32_t nextId_ = 1;          // ids are never reused within a session
    uint32_t selectedId_ = 0;      // 0: nothing selected
    QString fileName_;
    QTimer* reloadTimer_ = nullptr;
    bool filling_ = false;         // widgets are being filled from the graph: not an edit
    std::vector<uint32_t> bodyEntity_; // rigid body -> id of the entity that made it (from the last reload)

    std::vector<rf::SceneGraph> undo_, redo_;
    QElapsedTimer lastRemember_;

    uint32_t movingId_ = 0;        // the entity the mouse is dragging
    rf::Vector3 moveStartPosition_, moveStartHit_;

    std::vector<QAction*> createActions_;
    QAction *undoAct_ = nullptr, *redoAct_ = nullptr;

    // Scene list panel
    QWidget* listPanel_ = nullptr;
    QTreeWidget* list_ = nullptr;
    Vec3Row *gravity_ = nullptr, *worldSize_ = nullptr;
    QCheckBox *gas_ = nullptr, *plasma_ = nullptr;

    // Inspector panel
    QWidget* inspectorPanel_ = nullptr;
    QWidget* inspectorBody_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QLabel* headerIcon_ = nullptr;
    QLabel* headerName_ = nullptr;
    QWidget* headerChips_ = nullptr; // small role icons next to the name
    RoleBar* roleBar_ = nullptr;
    ObjectInspector* object_ = nullptr;
    QComboBox* shape_ = nullptr;
    Vec3Row* size_ = nullptr;
    CollapsibleSection* sections_[int(RoleIcon::Count)] = {};
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
