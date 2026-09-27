#pragma once
// The first minute of the editor: a bubble with an arrow that points at the next thing to press -
// the "Куб" button, then the tile "Твёрдое", then ▶ - so that three clicks make a cube fall. The
// bubble floats over the window (it is a child of it, not a dialog), follows its target when the
// window changes size, and goes for good after the cube falls, after anything else the user does
// first, or after its ✕ (MainWindow keeps "never again" in QSettings).
#include <QFrame>
#include <QPointer>

class QLabel;
class QToolButton;

class FirstStartHint : public QFrame {
    Q_OBJECT
public:
    explicit FirstStartHint(QWidget* window);
    // Shows `text` next to `target` with the arrow pointing at it: below a target at the top of the
    // window, to the left of a target in the right-hand panel.
    void pointAt(QWidget* target, const QString& text);
    QString text() const;
    QWidget* target() const { return target_; }

signals:
    void closeClicked();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override; // the window moved or changed size
    void paintEvent(QPaintEvent* event) override;

private:
    enum class Side { Above, Right }; // where the target is, seen from the bubble
    void place();

    QLabel* text_ = nullptr;
    QToolButton* close_ = nullptr;
    QPointer<QWidget> target_;
    Side side_ = Side::Above;
};
