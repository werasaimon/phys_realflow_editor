#pragma once
// The scene builder ("Конструктор"): make a scene without code. Buttons add shapes (cube, sphere,
// cylinder, cone, plane); the list on the left shows them; the inspector under it says what each
// one is - a rigid body, a soft body, liquid, a magnet, a heat source - and how it looks and moves.
// Everything is kept in an rf::SceneGraph (the SDK's scene-as-data, scene/SceneGraph.h) and, after
// every change, handed to the simulation as a GraphScene, so what you see is always the scene as
// written. The graph saves to a small text file (*.rfscene) anyone can read.
//
// Next step (not yet): a node view where roles are boxes you wire to entities.

#include "SimController.h"

#include "scene/SceneGraph.h"

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QComboBox;
class QTimer;
class QFormLayout;

class SceneBuilder : public QWidget {
    Q_OBJECT
public:
    explicit SceneBuilder(SimController* ctrl, QWidget* parent = nullptr);

    void newScene();                         // an empty world with a floor plane
    bool openFile(const QString& path, QString& error);
    bool saveFile(const QString& path, QString& error) const;
    void selectBody(int body);               // picking in the viewport: the entity of a rigid body
    const rf::SceneGraph& graph() const { return graph_; }

    // Three numbers in one row (x, y, z): position, size, rotation, velocity, moment, gravity.
    struct Vec3Edit {
        QDoubleSpinBox* x = nullptr;
        QDoubleSpinBox* y = nullptr;
        QDoubleSpinBox* z = nullptr;
    };

private:
    // Building the panel
    void buildToolbar(class QVBoxLayout* col);
    void buildOutliner(class QVBoxLayout* col);
    void buildEntityGroup(class QVBoxLayout* col);
    void buildRigidGroup(class QVBoxLayout* col);
    void buildSoftLiquidGroups(class QVBoxLayout* col);
    void buildMagnetHeatGroups(class QVBoxLayout* col);
    void buildWorldGroup(class QVBoxLayout* col);
    QGroupBox* roleGroup(class QVBoxLayout* col, const QString& title, QFormLayout*& form);
    QWidget* vec3Row(Vec3Edit& e, double min, double max, double step, int decimals);
    QDoubleSpinBox* spin(double min, double max, double step, int decimals);

    // Editing
    void addEntity(rf::ShapeKind shape);
    void removeSelected();
    void duplicateSelected();
    void onSelectionChanged();
    void onEdited();                // any inspector widget changed: write it into the graph
    void onRoleToggled(QGroupBox* box, bool on);
    void fillInspector();           // the selected entity -> the widgets
    void readInspector();           // the widgets -> the selected entity
    void fillWorld();
    void readWorld();
    void refreshOutliner();
    void pickColor();
    void scheduleReload();          // debounced: dragging a spin box does not reload 60 times
    void reload();                  // the graph -> the simulation (a new GraphScene)
    int selected() const;

    SimController* ctrl_;
    rf::SceneGraph graph_;
    QTimer* reloadTimer_ = nullptr;
    bool filling_ = false;          // the widgets are being filled from the graph: not an edit
    QString fileName_;

    QListWidget* outliner_ = nullptr;
    QWidget* inspector_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* shape_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    Vec3Edit size_, position_, rotation_;

    QGroupBox *rigidBox_ = nullptr, *softBox_ = nullptr, *liquidBox_ = nullptr, *magnetBox_ = nullptr, *heatBox_ = nullptr;
    QDoubleSpinBox *rigidDensity_ = nullptr, *friction_ = nullptr, *restitution_ = nullptr;
    QCheckBox* fixed_ = nullptr;
    Vec3Edit velocity_, spinRate_;
    QDoubleSpinBox *softDensity_ = nullptr, *stiffness_ = nullptr;
    Vec3Edit moment_;
    QDoubleSpinBox *temperature_ = nullptr, *smoke_ = nullptr;

    Vec3Edit gravity_, worldSize_;
    QCheckBox *gas_ = nullptr, *plasma_ = nullptr;
};
