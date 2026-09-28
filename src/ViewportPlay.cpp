// Two small things the viewport draws on top of its picture (see Viewport.h):
//   - the play mode's frame: an amber border round the view while the scene plays, so nobody edits
//     a running scene thinking it is the one that will be kept (docs/ui-research.md, item 1);
//   - the orbit pivot: while the camera orbits, a ring with a dot on the point it turns around, as
//     Maya and Blender show it (item 4) - the turn is no longer a mystery when the point is far away.
// And one courtesy to what floats over the view: the caption line tells how far right it reaches.
#include "Viewport.h"

#include <QFontMetrics>
#include <QPainter>

// The caption under the title, and how far right it reaches. paintGL draws it at x = 14 in a
// 9-point font; the same font measures it here. The play banner reads "captionRight" and moves
// down a row instead of covering the words (PlayOverlay::place).
void Viewport::setEditCaption(const QString& text) {
    editCaption_ = text;
    QFont small = font();
    small.setPointSizeF(9);
    setProperty("captionRight", 14 + QFontMetrics(small).horizontalAdvance(text));
    update();
}

void Viewport::setPlayFrame(bool on) {
    if (playFrame_ == on) return;
    playFrame_ = on;
    update();
}

void Viewport::setOrbitPivot(const QVector3D& pivot, bool on) {
    orbitPivot_ = pivot;
    if (orbiting_ == on && !on) return;
    orbiting_ = on;
    update();
}

void Viewport::drawPlayFrame(QPainter& p) {
    if (!playFrame_) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(240, 160, 32), 4.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(2, 2, -2, -2));
    p.restore();
}

// A dark ring under a light one (readable on any background) and a dot in the middle.
void Viewport::drawOrbitPivot(QPainter& p) {
    if (!orbiting_) return;
    const rf::Vector2 at = gizmoView().project(rf::Vector3(orbitPivot_.x(), orbitPivot_.y(), orbitPivot_.z()));
    if (at.x < 0 || at.y < 0 || at.x > width() || at.y > height()) return;
    const QPointF c(at.x, at.y);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 150), 4.0));
    p.drawEllipse(c, 9.0, 9.0);
    p.setPen(QPen(QColor(255, 255, 255, 230), 1.6));
    p.drawEllipse(c, 9.0, 9.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 196, 64));
    p.drawEllipse(c, 2.8, 2.8);
    p.restore();
}
