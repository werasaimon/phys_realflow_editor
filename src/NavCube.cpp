// The navigation cube (see NavCube.h): the view's rotation turns six axis balls and a small cube;
// the far ones are drawn first so the near ones cover them.
#include "NavCube.h"

#include <QPainter>
#include <QLineF>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

const QColor kAxisColour[3] = {QColor(235, 90, 90), QColor(110, 210, 110), QColor(100, 150, 255)};
const char* kAxisName[3] = {"X", "Y", "Z"};
constexpr float kArm = 0.40f; // ball distance from the centre, as a fraction of the widget

// The view's rotation applied to a world direction: x to the right, y up on screen, z towards the eye.
QVector3D turned(const QMatrix4x4& view, const QVector3D& d) { return view.mapVector(d); }

} // namespace

QRectF NavCube::rect(const QSize& viewport) const {
    return QRectF(viewport.width() - kSize - kMargin, kMargin, kSize, kSize);
}

QVector3D NavCube::axis(int k) {
    QVector3D d(0, 0, 0);
    d[k / 2] = (k % 2 == 0) ? 1.0f : -1.0f;
    return d;
}

QPointF NavCube::ballCentre(int k, const QMatrix4x4& view, const QSize& viewport) const {
    const QVector3D s = turned(view, axis(k));
    const QPointF c = rect(viewport).center();
    return c + QPointF(s.x(), -s.y()) * (kArm * kSize);
}

int NavCube::hit(const QPointF& p, const QMatrix4x4& view, const QSize& viewport) const {
    const QRectF r = rect(viewport);
    if (QLineF(p, r.center()).length() > 0.5f * kSize) return kOutside;
    int best = kInside;
    float nearest = -1e9f;
    for (int k = 0; k < 6; ++k) { // the ball nearest the eye wins where two overlap
        if (QLineF(p, ballCentre(k, view, viewport)).length() > kBall + 2.0f) continue;
        const float z = turned(view, axis(k)).z();
        if (z > nearest) nearest = z, best = k;
    }
    return best;
}

// A cube of half the arm, its faces shaded by how much they face the eye, drawn back to front.
void NavCube::drawCube(QPainter& p, const QMatrix4x4& view, const QPointF& centre) const {
    const float h = 0.18f * kSize;
    std::array<QPointF, 8> corner;
    for (int c = 0; c < 8; ++c) {
        const QVector3D s = turned(view, QVector3D((c & 1) ? 1 : -1, (c & 2) ? 1 : -1, (c & 4) ? 1 : -1));
        corner[size_t(c)] = centre + QPointF(s.x(), -s.y()) * h;
    }
    const int faces[6][4] = {{1, 3, 7, 5}, {0, 4, 6, 2}, {2, 6, 7, 3}, {0, 1, 5, 4}, {4, 5, 7, 6}, {0, 2, 3, 1}};
    for (int f = 0; f < 6; ++f) {
        const float facing = turned(view, axis(f)).z();
        if (facing <= 0.02f) continue; // only the faces towards the eye
        QPainterPath face;
        face.moveTo(corner[size_t(faces[f][0])]);
        for (int i = 1; i < 4; ++i) face.lineTo(corner[size_t(faces[f][i])]);
        face.closeSubpath();
        const int shade = int(70 + 80 * facing);
        p.setPen(QPen(QColor(20, 22, 26, 180), 1.0));
        p.setBrush(QColor(shade, shade + 4, shade + 12, 235));
        p.drawPath(face);
    }
}

void NavCube::draw(QPainter& p, const QMatrix4x4& view, const QSize& viewport, int hover) const {
    const QRectF r = rect(viewport);
    const QPointF c = r.center();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, hover == kOutside ? 14 : 34)); // the disc brightens under the mouse
    p.drawEllipse(c, 0.5 * kSize, 0.5 * kSize);
    std::array<int, 6> order{0, 1, 2, 3, 4, 5};
    std::sort(order.begin(), order.end(), [&](int a, int b) { return turned(view, axis(a)).z() < turned(view, axis(b)).z(); });
    bool cubeDrawn = false;
    for (int k : order) {
        if (!cubeDrawn && turned(view, axis(k)).z() > 0.0f) drawCube(p, view, c), cubeDrawn = true;
        const QPointF b = ballCentre(k, view, viewport);
        const QColor col = kAxisColour[k / 2];
        const bool positive = k % 2 == 0;
        if (positive) {
            p.setPen(QPen(col.darker(130), 2.0));
            p.drawLine(c, b);
        }
        p.setPen(QPen(k == hover ? QColor(255, 230, 120) : col, k == hover ? 2.5 : 1.6));
        p.setBrush(positive ? col : QColor(col.red(), col.green(), col.blue(), 70));
        p.drawEllipse(b, kBall, kBall);
        if (positive) {
            p.setPen(QColor(20, 20, 24));
            QFont f = p.font();
            f.setBold(true);
            f.setPointSizeF(8.0);
            p.setFont(f);
            p.drawText(QRectF(b.x() - kBall, b.y() - kBall, 2 * kBall, 2 * kBall), Qt::AlignCenter, kAxisName[k / 2]);
        }
    }
    if (!cubeDrawn) drawCube(p, view, c);
    p.restore();
}
