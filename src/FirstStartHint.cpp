// The first-minute bubble (see FirstStartHint.h): an amber rounded box with a small arrow, drawn by
// hand so the arrow can sit on the side that faces the target.
#include "FirstStartHint.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

#include <algorithm>

namespace {
constexpr int kArrow = 12; // how far the arrow sticks out of the box, px
const QColor kFill(255, 209, 102);
const QColor kInk(38, 32, 20);
} // namespace

// A frameless tool-tip window of its own, not a child drawn inside the main window: the 3D view is
// an OpenGL surface, and a child widget over it was cut off where the view began (the bubble showed
// only its first letters). The main window stays its owner (lifetime, and it follows its moves).
FirstStartHint::FirstStartHint(QWidget* window) : QFrame(window, Qt::ToolTip | Qt::FramelessWindowHint) {
    setObjectName("firstStartHint");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(16, 12 + kArrow, 10, 12);
    text_ = new QLabel;
    text_->setWordWrap(true);
    text_->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: 600;").arg(kInk.name()));
    row->addWidget(text_, 1);
    close_ = new QToolButton;
    close_->setText("✕");
    close_->setToolTip("Больше не показывать");
    close_->setAutoRaise(true);
    close_->setStyleSheet(QString("color: %1; border: none; font-size: 13px;").arg(kInk.name()));
    row->addWidget(close_, 0, Qt::AlignTop);
    connect(close_, &QToolButton::clicked, this, &FirstStartHint::closeClicked);
    window->installEventFilter(this);
    hide();
}

QString FirstStartHint::text() const { return isVisible() ? text_->text() : QString(); }

void FirstStartHint::pointAt(QWidget* target, const QString& text) {
    target_ = target;
    text_->setText(text);
    text_->ensurePolished(); // the style sheet's font, for the width of one line
    text_->setFixedWidth(std::min(300, text_->fontMetrics().horizontalAdvance(text) + 6));
    const QPoint inWindow = target ? target->mapTo(target->window(), QPoint(0, 0)) : QPoint();
    side_ = inWindow.x() > parentWidget()->width() * 2 / 3 && inWindow.y() > 150 ? Side::Right : Side::Above;
    // The arrow's room: on top for a target above, on the right for a target to the right.
    auto* row = static_cast<QHBoxLayout*>(layout());
    row->setContentsMargins(16, 12 + (side_ == Side::Above ? kArrow : 0), 10 + (side_ == Side::Right ? kArrow : 0), 12);
    adjustSize();
    place();
    show();
    raise();
}

// Below the target with the arrow up to its centre, or to its left with the arrow pointing right.
// In screen coordinates (the bubble is a window of its own), kept inside the main window.
void FirstStartHint::place() {
    if (!target_ || !target_->isVisible()) return;
    const QRect r(target_->mapToGlobal(QPoint(0, 0)), target_->size());
    const QRect win(parentWidget()->mapToGlobal(QPoint(0, 0)), parentWidget()->size());
    QPoint topLeft = side_ == Side::Above ? QPoint(r.center().x() - 40, r.bottom() + 2)
                                          : QPoint(r.left() - width() - 2, r.center().y() - height() / 2);
    topLeft.setX(std::clamp(topLeft.x(), win.left() + 8, std::max(win.left() + 8, win.right() - width() - 8)));
    move(topLeft);
    update();
}

bool FirstStartHint::eventFilter(QObject* watched, QEvent* event) {
    const QEvent::Type t = event->type();
    if (watched == parentWidget() && (t == QEvent::Resize || t == QEvent::Move || t == QEvent::LayoutRequest)) place();
    // A window of its own does not hide with the main window: follow it.
    if (watched == parentWidget() && t == QEvent::WindowStateChange && isVisible() && parentWidget()->isMinimized()) hide();
    return QFrame::eventFilter(watched, event);
}

void FirstStartHint::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF box = rect().adjusted(1, 1, -1, -1);
    if (side_ == Side::Above) box.setTop(box.top() + kArrow);
    else box.setRight(box.right() - kArrow);
    QPainterPath path;
    path.addRoundedRect(box, 10, 10);
    QPolygonF arrow;
    if (side_ == Side::Above && target_) { // the tip at the target's centre, seen from this widget
        const float centre = float(target_->mapToGlobal(target_->rect().center()).x() - x()); // both in screen coordinates
        const float tip = std::clamp(centre, 20.0f, float(width() - 20));
        arrow << QPointF(tip - 9, box.top() + 1) << QPointF(tip, 1) << QPointF(tip + 9, box.top() + 1);
    } else {
        const float mid = float(height()) / 2;
        arrow << QPointF(box.right() - 1, mid - 9) << QPointF(width() - 1, mid) << QPointF(box.right() - 1, mid + 9);
    }
    path.addPolygon(arrow);
    path.setFillRule(Qt::WindingFill);
    p.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
    p.setBrush(kFill);
    p.drawPath(path.simplified());
}
