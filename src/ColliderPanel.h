#pragma once
// "Коллайдер — чем сталкивается": the part of the rigid role that says what the body collides
// with, apart from what it looks like (Houdini's collision guide of a packed RBD object, Unity's
// Collider next to its MeshRenderer). A row of icon buttons picks the kind - Авто (the geometry
// itself), Коробка, Сфера, Капсула, Выпуклая оболочка, Точно (convex parts) - and "Подогнать под
// геометрию" sizes it to the geometry; switched off, size, offset and turn are typed in. Choosing a
// collider never changes the geometry, and the geometry's dropdown never changes the collider.
#include "rigid/Shapes.h"
#include "scene/SceneGraph.h"

#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QLabel;
class Vec3Row;

class ColliderPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColliderPanel(QWidget* parent = nullptr);
    void setCollider(const rf::ColliderRole& c); // does not report a change
    void writeTo(rf::ColliderRole& c) const;     // the kind, the fit and the numbers; not `enabled`
    // What "Авто" turned into for this geometry ("сфера", "выпуклая оболочка"), shown next to it.
    void setAutoResult(const QString& what);

signals:
    void edited(); // the user changed the kind, the fit or a number

private:
    void buildKindRow(class QVBoxLayout* col);
    void buildManualFields(class QVBoxLayout* col);
    void updateVisibility(); // the fields only when they mean something for the chosen kind
    void onKindClicked(int kind);

    QButtonGroup* kinds_ = nullptr;
    QLabel* kindName_ = nullptr;
    QCheckBox* fit_ = nullptr;
    QWidget* manual_ = nullptr; // size, offset, turn: shown when not fitted to the geometry
    QWidget* sizeRow_ = nullptr;
    Vec3Row *size_ = nullptr, *offset_ = nullptr, *rotation_ = nullptr;
    rf::ColliderKind kind_ = rf::ColliderKind::Auto;
    QString autoResult_;
    bool filling_ = false;
};

// The kinds as the editor names them: "Авто", "Коробка", "Сфера", "Капсула", "Выпуклая оболочка", "Точно".
QString colliderName(rf::ColliderKind k);
// A collision shape in words, lower case: "коробка", "сфера", "капсула", "выпуклая оболочка", "выпуклые части".
QString collisionShapeName(rf::ShapeType t);
