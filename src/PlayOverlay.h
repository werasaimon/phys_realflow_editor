#pragma once
// What lies on top of the 3D view to say "the scene is playing" and to drive it (docs/ui-research.md,
// items 1, 2 and 10): the reviews of Unity, Unreal and Godot say people lose their work because they
// edit during play without noticing it. So while the scene plays:
//   - the view gets a coloured frame (Viewport::setPlayFrame) and
//   - a banner at the top: "ИГРА — правки не сохранятся" with a button "Сохранить положения (K)";
// and always, at the bottom centre, big half-transparent ▶ ⏸ ■ buttons - the toolbar's own actions,
// so their state (enabled, disabled) is one and the same.
// The widgets are children of the view; they follow its size through an event filter.
#include <QObject>

class QAction;
class QFrame;
class QLabel;
class QPushButton;
class QWidget;

class PlayOverlay : public QObject {
    Q_OBJECT
public:
    PlayOverlay(QWidget* view, QAction* play, QAction* pause, QAction* stop, QAction* keep);
    // Edit: no banner. Playing / paused: the banner (its words say which); `canKeep`: the builder's
    // scene (a ready-made sample has nothing to keep, so no button).
    void setState(bool playing, bool paused, bool canKeep);
    // The big buttons off (the gallery's thumbnails, the first-minute bubble's own ▶).
    void setControlsVisible(bool on);
    QFrame* banner() const { return banner_; }
    QFrame* controls() const { return controls_; }

protected:
    bool eventFilter(QObject* watched, class QEvent* event) override;

private:
    void buildBanner(QAction* keep);
    void buildControls(QAction* play, QAction* pause, QAction* stop);
    void place(); // the banner at the top centre, the buttons at the bottom centre

    QWidget* view_;
    QFrame* banner_ = nullptr;
    QLabel* bannerText_ = nullptr;
    QPushButton* keepButton_ = nullptr;
    QFrame* controls_ = nullptr;
};
