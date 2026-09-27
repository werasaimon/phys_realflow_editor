#pragma once
// Small building blocks of the inspector, shared by every kind of thing it shows (a shape today, a
// camera or a light later): three numbers in a row (x, y, z), a spin box that reports only real
// edits, a section with a header that folds open and shut, and a small-caps section title.
#include "math/Vector3.h"

#include <QDoubleSpinBox>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

// A number field for the inspector: fixed range, step and decimals; typing counts only on Enter.
QDoubleSpinBox* makeSpin(double min, double max, double step, int decimals, const QString& suffix = {});

// A small-caps title that starts a group of fields ("ОБЪЕКТ", "ФОРМА").
QLabel* sectionTitle(const QString& text);

// Three numbers in one row: position, size, rotation, velocity, magnetic moment, gravity.
class Vec3Row : public QWidget {
    Q_OBJECT
public:
    Vec3Row(double min, double max, double step, int decimals, QWidget* parent = nullptr);
    void setValue(const rf::Vector3& v); // does not report a change
    rf::Vector3 value() const;

signals:
    void edited(); // the user changed one of the three numbers

private:
    QDoubleSpinBox* spins_[3];
};

// A section whose details fold away: a header button with an arrow and a title, and a body that
// shows only when opened. Starts closed, so a beginner sees titles, not forty fields.
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    CollapsibleSection(const QString& title, const QIcon& icon = {}, QWidget* parent = nullptr);
    QVBoxLayout* body() { return bodyLayout_; }
    void setOpen(bool open);
    bool isOpen() const { return open_; }

private:
    QPushButton* header_ = nullptr;
    QWidget* bodyWidget_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
    bool open_ = false;
};
