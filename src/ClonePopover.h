#pragma once
// The small window at the cursor after a Shift + gizmo drag (3ds Max's Clone Options, smaller):
// "Клонировать", how many copies (1 to 1000), of what kind - ● Копия ○ Экземпляр ○ Массив - and
// OK / Отмена. Enter is OK, Esc is Отмена, and a click past it counts as Отмена too. It is a popup:
// while it is open nothing else in the window takes the mouse.
#include <QFrame>

class QRadioButton;
class QSpinBox;

class ClonePopover : public QFrame {
    Q_OBJECT
public:
    explicit ClonePopover(QWidget* parent = nullptr);
    // Экземпляр and Массив may be unavailable (a scale cannot be repeated by them): greyed, with why.
    void allowKinds(bool instance, bool array);
    void popup(const QPoint& globalPos); // shows it with its corner at the cursor, kept on the screen
    int copies() const;
    int kind() const; // 0 Копия, 1 Экземпляр, 2 Массив (SceneBuilder::CloneKind)

signals:
    void accepted(int copies, int kind);
    void cancelled();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void hideEvent(QHideEvent* e) override; // closed without OK: cancelled

private:
    void accept();

    QSpinBox* count_ = nullptr;
    QRadioButton* kinds_[3] = {};
    bool answered_ = false;
};
