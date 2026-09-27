#pragma once
// The "Объект" block of the inspector: what every thing in a scene has, whatever it is - a shape
// with roles today, a camera or a light later (rf::SceneObject in the SDK): its name, where it is,
// how it is turned, its colour, and two switches - the eye (visible: hidden things take no part
// in the simulation) and the lock (the mouse neither selects nor moves it). The block is the same
// for every kind of thing, so it looks and works the same everywhere.
#include "scene/SceneGraph.h"

#include <QWidget>

class QLineEdit;
class QPushButton;
class QToolButton;
class Vec3Row;

class ObjectInspector : public QWidget {
    Q_OBJECT
public:
    explicit ObjectInspector(QWidget* parent = nullptr);

    void setObject(const rf::SceneObject& o); // fills the fields; not an edit
    void writeTo(rf::SceneObject& o) const;  // the fields -> the object (id stays)
    void setPosition(const rf::Vector3& p); // live update while the mouse drags the object
    // Several objects selected: an empty name and "—" in the numbers that differ between them
    // (bit k of a mask: axis k). Call after setObject; an edit then goes only to what was changed.
    void showMixed(bool name, int positionMask, int rotationMask);

signals:
    void edited(); // any field changed by the user

private:
    void pickColour();
    void updateToggleIcons();

    QLineEdit* name_ = nullptr;
    QToolButton* visible_ = nullptr;
    QToolButton* locked_ = nullptr;
    QPushButton* colour_ = nullptr;
    QColor colourValue_;
    Vec3Row* position_ = nullptr;
    Vec3Row* rotation_ = nullptr;
};
