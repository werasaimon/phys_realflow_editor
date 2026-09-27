// The play banner and the big ▶ ⏸ ■ buttons on top of the 3D view (see PlayOverlay.h).
#include "PlayOverlay.h"

#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>

PlayOverlay::PlayOverlay(QWidget* view, QAction* play, QAction* pause, QAction* stop, QAction* keep)
    : QObject(view), view_(view) {
    buildBanner(keep);
    buildControls(play, pause, stop);
    view_->installEventFilter(this);
    setState(false, false, false);
    place();
}

void PlayOverlay::buildBanner(QAction* keep) {
    banner_ = new QFrame(view_);
    banner_->setObjectName("playBanner");
    auto* row = new QHBoxLayout(banner_);
    row->setContentsMargins(12, 6, 8, 6);
    row->setSpacing(10);
    bannerText_ = new QLabel;
    row->addWidget(bannerText_);
    keepButton_ = new QPushButton("Сохранить положения (K)");
    keepButton_->setObjectName("keepButton");
    keepButton_->setToolTip("Положения тел сейчас останутся в сцене и после ■ Стоп "
                            "(выбранных — или всех, если ничего не выбрано). Как Keep Simulation Changes в Unreal");
    keepButton_->setCursor(Qt::PointingHandCursor);
    connect(keepButton_, &QPushButton::clicked, keep, &QAction::trigger);
    row->addWidget(keepButton_);
}

// Three round buttons, each showing one of the toolbar's actions (its icon, tooltip and state).
void PlayOverlay::buildControls(QAction* play, QAction* pause, QAction* stop) {
    controls_ = new QFrame(view_);
    controls_->setObjectName("bigControls");
    auto* row = new QHBoxLayout(controls_);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(4);
    for (QAction* a : {play, pause, stop}) {
        auto* b = new QToolButton;
        b->setObjectName("bigControl");
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(34, 34));
        b->setCursor(Qt::PointingHandCursor);
        row->addWidget(b);
    }
}

void PlayOverlay::setState(bool playing, bool paused, bool canKeep) {
    const bool running = playing || paused;
    view_->setProperty("playing", running);
    banner_->setVisible(running);
    bannerText_->setText(paused ? "⏸ ПАУЗА — правки не сохранятся после ■ Стоп"
                                : "▶ ИГРА — правки не сохранятся после ■ Стоп");
    keepButton_->setVisible(canKeep);
    banner_->adjustSize();
    place();
}

void PlayOverlay::setControlsVisible(bool on) {
    controls_->setVisible(on);
}

bool PlayOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_ && event->type() == QEvent::Resize) place();
    return QObject::eventFilter(watched, event);
}

void PlayOverlay::place() {
    const int w = view_->width(), h = view_->height();
    banner_->adjustSize();
    banner_->move((w - banner_->width()) / 2, 10);
    controls_->adjustSize();
    controls_->move((w - controls_->width()) / 2, h - controls_->height() - 14);
    banner_->raise();
    controls_->raise();
}
