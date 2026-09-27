#pragma once
// Small building blocks of the inspector, shared by every kind of thing it shows (a shape today, a
// camera or a light later): three numbers in a row (x, y, z), a spin box that reports only real
// edits, a section with a header that folds open and shut, and a small-caps section title.
#include "math/Vector3.h"

#include <QDoubleSpinBox>
#include <QFrame>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QToolButton;
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

// A component of an object as a card, as in Unity's inspector: a header with the component's icon
// and title that folds the card open and shut, and an ✕ that removes the component. Opens when
// added, so the numbers of what was just added are in view.
class ComponentCard : public QFrame {
    Q_OBJECT
public:
    ComponentCard(const QString& title, const QIcon& icon, QWidget* parent = nullptr);
    QVBoxLayout* body() { return bodyLayout_; }
    void setOpen(bool open);
    bool isOpen() const { return open_; }
    const QString& title() const { return title_; }

signals:
    void removeClicked();

private:
    QPushButton* header_ = nullptr;
    QToolButton* remove_ = nullptr;
    QWidget* bodyWidget_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
    QString title_;
    bool open_ = true;
};

// One sentence saying why something does not work (yet) and, where possible, a button that fixes
// it: "Дыму нужен воздух — [Включить газ]". No dialog to click away: the banner sits in the panel
// until the cause is gone.
class InlineBanner : public QFrame {
    Q_OBJECT
public:
    explicit InlineBanner(QWidget* parent = nullptr);
    void showMessage(const QString& text, const QString& buttonText = {}, std::function<void()> fix = {});
    void clearMessage();
    QString message() const; // what it says now (empty: hidden)

private:
    QLabel* text_ = nullptr;
    QPushButton* button_ = nullptr;
    std::function<void()> fix_;
};
