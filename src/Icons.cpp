// The editor's icons (see Icons.h), each painted on a 100 x 100 canvas that is scaled to the
// requested size. Light comes from the top left: faces turned to it are lighter, the others darker,
// and a soft shadow sits under every shape so it reads as a solid thing standing on a floor.
#include "Icons.h"

#include <QDir>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <cmath>
#include <functional>

namespace {

using Draw = std::function<void(QPainter&)>;

// One pixmap of the icon at a screen scale (1 = normal, 2 = high-DPI screens).
QPixmap render(int size, qreal scale, const Draw& draw) {
    QPixmap pm(int(size * scale), int(size * scale));
    pm.setDevicePixelRatio(scale);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 100.0, size / 100.0);
    draw(p);
    return pm;
}

QIcon makeIcon(int size, const Draw& draw) {
    QIcon icon;
    icon.addPixmap(render(size, 1.0, draw));
    icon.addPixmap(render(size, 2.0, draw));
    return icon;
}

QColor shade(const QColor& c, int factor) { return factor >= 100 ? c.lighter(factor) : c.darker(200 - factor); }

// A soft contact shadow under a shape: it stands on something.
void shadow(QPainter& p, qreal cx, qreal cy, qreal rx) {
    QRadialGradient g(QPointF(cx, cy), rx);
    g.setColorAt(0, QColor(0, 0, 0, 90));
    g.setColorAt(1, QColor(0, 0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(QPointF(cx, cy), rx, rx * 0.28);
}

QPen outline(const QColor& c) { return QPen(shade(c, 55), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------
void drawCube(QPainter& p, const QColor& c) {
    shadow(p, 50, 89, 40);
    const QPointF top[] = {{50, 12}, {86, 30}, {50, 48}, {14, 30}};
    const QPointF left[] = {{14, 30}, {50, 48}, {50, 88}, {14, 70}};
    const QPointF right[] = {{86, 30}, {50, 48}, {50, 88}, {86, 70}};
    p.setPen(outline(c));
    p.setBrush(shade(c, 135));
    p.drawPolygon(top, 4);
    p.setBrush(c);
    p.drawPolygon(left, 4);
    p.setBrush(shade(c, 70));
    p.drawPolygon(right, 4);
}

void drawSphere(QPainter& p, const QColor& c) {
    shadow(p, 52, 89, 34);
    QRadialGradient g(QPointF(38, 36), 52, QPointF(34, 32));
    g.setColorAt(0.0, shade(c, 165));
    g.setColorAt(0.45, c);
    g.setColorAt(1.0, shade(c, 45));
    p.setPen(outline(c));
    p.setBrush(g);
    p.drawEllipse(QPointF(50, 50), 36, 36);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 150));
    p.drawEllipse(QPointF(37, 34), 8, 5);
}

// A body lit from the left: light at the left third, dark at the right edge.
QLinearGradient sideLight(const QColor& c, qreal x0, qreal x1) {
    QLinearGradient g(QPointF(x0, 0), QPointF(x1, 0));
    g.setColorAt(0.0, shade(c, 110));
    g.setColorAt(0.3, shade(c, 130));
    g.setColorAt(1.0, shade(c, 55));
    return g;
}

void drawCylinder(QPainter& p, const QColor& c) {
    shadow(p, 50, 90, 36);
    QPainterPath body;
    body.moveTo(20, 26);
    body.lineTo(20, 78);
    body.arcTo(QRectF(20, 68, 60, 20), 180, 180);
    body.lineTo(80, 26);
    body.closeSubpath();
    p.setPen(outline(c));
    p.setBrush(sideLight(c, 20, 80));
    p.drawPath(body);
    p.setBrush(shade(c, 140));
    p.drawEllipse(QRectF(20, 16, 60, 20));
}

void drawCone(QPainter& p, const QColor& c) {
    shadow(p, 50, 90, 36);
    QPainterPath body;
    body.moveTo(50, 10);
    body.lineTo(18, 78);
    body.arcTo(QRectF(18, 68, 64, 20), 180, 180);
    body.closeSubpath();
    p.setPen(outline(c));
    p.setBrush(sideLight(c, 18, 82));
    p.drawPath(body);
}

void drawPlane(QPainter& p, const QColor& c) {
    const QPointF top[] = {{8, 58}, {50, 38}, {92, 58}, {50, 78}};
    const QPointF edgeL[] = {{8, 58}, {50, 78}, {50, 84}, {8, 64}};
    const QPointF edgeR[] = {{92, 58}, {50, 78}, {50, 84}, {92, 64}};
    p.setPen(outline(c));
    p.setBrush(shade(c, 60));
    p.drawPolygon(edgeR, 4);
    p.setBrush(shade(c, 85));
    p.drawPolygon(edgeL, 4);
    p.setBrush(shade(c, 125));
    p.drawPolygon(top, 4);
    p.setPen(QPen(shade(c, 90), 1.0));
    for (int k = 1; k < 4; ++k) { // a grid on the top: it is a floor
        const qreal t = k / 4.0;
        p.drawLine(QPointF(8 + 42 * t, 58 - 20 * t), QPointF(50 + 42 * t, 78 - 20 * t));
        p.drawLine(QPointF(8 + 42 * t, 58 + 20 * t), QPointF(50 + 42 * t, 38 + 20 * t));
    }
}

// A model from a file: a faceted low-poly gem, each facet in its own shade.
void drawModel(QPainter& p, const QColor& c) {
    shadow(p, 50, 90, 36);
    QPointF ring[6];
    for (int k = 0; k < 6; ++k) {
        const qreal a = (90 + 60 * k) * 3.14159265 / 180;
        ring[k] = QPointF(50 + 38 * std::cos(a), 52 - 38 * std::sin(a));
    }
    const QPointF centre(50, 46);
    const int shades[6] = {140, 120, 95, 70, 80, 110};
    p.setPen(outline(c));
    for (int k = 0; k < 6; ++k) {
        const QPointF tri[] = {centre, ring[k], ring[(k + 1) % 6]};
        p.setBrush(shade(c, shades[k]));
        p.drawPolygon(tri, 3);
    }
}

QColor shapeColor(rf::ShapeKind k) {
    switch (k) {
    case rf::ShapeKind::Box: return QColor(232, 128, 72);
    case rf::ShapeKind::Sphere: return QColor(78, 150, 235);
    case rf::ShapeKind::Cylinder: return QColor(110, 196, 98);
    case rf::ShapeKind::Cone: return QColor(236, 196, 80);
    case rf::ShapeKind::Plane: return QColor(160, 166, 176);
    case rf::ShapeKind::Mesh: return QColor(170, 120, 220);
    }
    return QColor(180, 180, 180);
}

// ---------------------------------------------------------------------------
// Roles: what a shape is made of
// ---------------------------------------------------------------------------
void drawJelly(QPainter& p) {
    const QColor c(98, 205, 110);
    shadow(p, 50, 88, 38);
    QPainterPath blob;
    blob.moveTo(20, 60);
    blob.cubicTo(18, 36, 30, 26, 50, 28);
    blob.cubicTo(72, 26, 84, 38, 82, 60);
    blob.cubicTo(82, 80, 66, 86, 50, 84);
    blob.cubicTo(32, 86, 20, 80, 20, 60);
    QRadialGradient g(QPointF(40, 42), 50, QPointF(36, 38));
    g.setColorAt(0, QColor(200, 255, 200, 235));
    g.setColorAt(0.5, QColor(c.red(), c.green(), c.blue(), 225));
    g.setColorAt(1, QColor(30, 110, 50, 235));
    p.setPen(outline(c));
    p.setBrush(g);
    p.drawPath(blob);
    p.setPen(QPen(QColor(255, 255, 255, 170), 3.5, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(28, 34, 26, 22), 100 * 16, 70 * 16); // wet highlight
}

void drawDrop(QPainter& p) {
    const QColor c(64, 150, 240);
    QPainterPath drop;
    drop.moveTo(50, 8);
    drop.cubicTo(40, 28, 20, 44, 20, 62);
    drop.arcTo(QRectF(20, 32, 60, 60), 180, 180);
    drop.cubicTo(80, 44, 60, 28, 50, 8);
    QRadialGradient g(QPointF(40, 56), 46, QPointF(36, 50));
    g.setColorAt(0, QColor(200, 232, 255));
    g.setColorAt(0.5, c);
    g.setColorAt(1, QColor(20, 70, 150));
    p.setPen(outline(c));
    p.setBrush(g);
    p.drawPath(drop);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 170));
    p.drawEllipse(QPointF(37, 58), 5, 9);
}

void drawCloth(QPainter& p) {
    const QColor c(222, 96, 112);
    p.setPen(QPen(QColor(150, 155, 165), 5, Qt::SolidLine, Qt::RoundCap)); // the rod
    p.drawLine(QPointF(10, 16), QPointF(90, 16));
    QPainterPath sheet;
    sheet.moveTo(16, 18);
    sheet.lineTo(84, 18);
    sheet.cubicTo(88, 44, 80, 62, 86, 86);
    sheet.cubicTo(74, 80, 64, 92, 52, 86);
    sheet.cubicTo(40, 80, 30, 92, 16, 86);
    sheet.cubicTo(22, 62, 12, 44, 16, 18);
    QLinearGradient g(QPointF(16, 0), QPointF(84, 0));
    for (int k = 0; k <= 6; ++k) g.setColorAt(k / 6.0, k % 2 ? shade(c, 70) : shade(c, 118)); // folds
    p.setPen(outline(c));
    p.setBrush(g);
    p.drawPath(sheet);
}

// ---------------------------------------------------------------------------
// Roles: what a shape also does
// ---------------------------------------------------------------------------
void drawMagnet(QPainter& p) {
    const QRectF bend(31, 38, 38, 38);
    auto leg = [&](qreal x, qreal start, qreal sweep, const QColor& col) {
        QPainterPath path;
        path.moveTo(x, 26);
        path.lineTo(x, 57);
        path.arcTo(bend, start, sweep);
        p.setPen(QPen(col, 17, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.drawPath(path);
    };
    shadow(p, 50, 92, 30);
    leg(31, 180, 90, QColor(214, 58, 58));  // north: red
    leg(69, 0, -90, QColor(52, 104, 220));  // south: blue
    p.setPen(QPen(QColor(205, 210, 218), 17, Qt::SolidLine, Qt::FlatCap)); // bare iron tips
    p.drawLine(QPointF(31, 12), QPointF(31, 26));
    p.drawLine(QPointF(69, 12), QPointF(69, 26));
}

void drawSmoke(QPainter& p) {
    struct Puff { qreal x, y, r; int grey; };
    const Puff puffs[] = {{36, 66, 20, 150}, {62, 64, 22, 135}, {50, 46, 22, 170}, {70, 36, 13, 185}, {80, 18, 8, 200}};
    p.setPen(Qt::NoPen);
    for (const Puff& f : puffs) {
        QRadialGradient g(QPointF(f.x - f.r * 0.35, f.y - f.r * 0.35), f.r * 1.3);
        g.setColorAt(0, QColor(f.grey + 50, f.grey + 50, f.grey + 55, 240));
        g.setColorAt(1, QColor(f.grey - 40, f.grey - 40, f.grey - 35, 225));
        p.setBrush(g);
        p.drawEllipse(QPointF(f.x, f.y), f.r, f.r);
    }
}

void drawFlame(QPainter& p) {
    auto flame = [&](qreal scale, qreal dy, const QColor& inner, const QColor& outer) {
        QPainterPath f;
        f.moveTo(50, 8 + dy);
        f.cubicTo(58, 30, 82, 42, 78, 64);
        f.cubicTo(76, 82, 62, 92, 50, 92);
        f.cubicTo(38, 92, 24, 82, 22, 64);
        f.cubicTo(20, 50, 34, 44, 36, 28);
        f.cubicTo(44, 38, 44, 46, 46, 50);
        f.cubicTo(50, 34, 44, 22, 50, 8 + dy);
        QTransform t;
        t.translate(50, 92);
        t.scale(scale, scale);
        t.translate(-50, -92);
        QLinearGradient g(QPointF(0, 92), QPointF(0, 10));
        g.setColorAt(0, inner);
        g.setColorAt(1, outer);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(t.map(f));
    };
    flame(1.0, 0, QColor(255, 120, 30), QColor(220, 40, 20));
    flame(0.58, 8, QColor(255, 245, 160), QColor(255, 170, 40));
}

void drawThermometer(QPainter& p) {
    p.setPen(QPen(QColor(90, 96, 108), 2.2));
    p.setBrush(QColor(238, 241, 246));
    p.drawRoundedRect(QRectF(41, 8, 18, 70), 9, 9);
    p.setBrush(QColor(228, 60, 52));
    p.drawEllipse(QPointF(50, 78), 14, 14);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(46, 30, 8, 46), 4, 4); // the red column
    p.setBrush(QColor(255, 255, 255, 150));
    p.drawEllipse(QPointF(45, 74), 4, 5);
    p.setPen(QPen(QColor(90, 96, 108), 2));
    for (int k = 0; k < 4; ++k) p.drawLine(QPointF(62, 18 + 12 * k), QPointF(70, 18 + 12 * k));
}

void drawRigidBlock(QPainter& p) { drawCube(p, QColor(150, 158, 172)); }

// ---------------------------------------------------------------------------
// Controls: one light colour on the dark toolbar, green for "run"
// ---------------------------------------------------------------------------
const QColor kControl(222, 226, 234);

void drawArrowArc(QPainter& p, bool clockwise) {
    p.setPen(QPen(kControl, 9, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    const QRectF r(20, 26, 60, 56);
    p.drawArc(r, (clockwise ? 150 : 30) * 16, (clockwise ? -170 : 170) * 16);
    const QPointF tip = clockwise ? QPointF(76, 42) : QPointF(24, 42);
    const qreal s = clockwise ? 1 : -1;
    const QPointF head[] = {tip + QPointF(s * 8, -16), tip + QPointF(s * 10, 10), tip + QPointF(-s * 14, -2)};
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawPolygon(head, 3);
}

// "Back to the start": a nearly full circle running clockwise, its head pointing up at the left.
void drawCircleArrow(QPainter& p) {
    p.setPen(QPen(kControl, 9, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(20, 20, 60, 60), 100 * 16, -280 * 16);
    const QPointF head[] = {{21, 30}, {10, 48}, {32, 48}};
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawPolygon(head, 3);
}

void drawEye(QPainter& p, bool open) {
    QPainterPath almond;
    almond.moveTo(8, 50);
    almond.cubicTo(28, 22, 72, 22, 92, 50);
    almond.cubicTo(72, 78, 28, 78, 8, 50);
    p.setPen(QPen(kControl, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(almond);
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawEllipse(QPointF(50, 50), 13, 13);
    if (!open) {
        p.setPen(QPen(QColor(236, 96, 96), 8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(16, 84), QPointF(84, 16));
    }
}

void drawLock(QPainter& p, bool closed) {
    p.setPen(QPen(kControl, 8, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    QPainterPath shackle;
    shackle.moveTo(32, 46);
    shackle.lineTo(32, 32);
    shackle.arcTo(QRectF(32, 14, 36, 36), 180, -180);
    shackle.lineTo(68, closed ? 46 : 34);
    if (!closed) shackle.translate(12, -6);
    p.drawPath(shackle);
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawRoundedRect(QRectF(22, 46, 56, 42), 8, 8);
    p.setBrush(QColor(60, 64, 72));
    p.drawEllipse(QPointF(50, 64), 6, 6);
}

// The edit tools: a cursor arrow, a cross of four arrows, a turning arrow, a growing square.
void drawCursor(QPainter& p) {
    const QPointF arrow[] = {{30, 12}, {30, 80}, {46, 64}, {58, 90}, {70, 84}, {58, 58}, {80, 58}};
    p.setPen(QPen(QColor(40, 44, 52), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(kControl);
    p.drawPolygon(arrow, 7);
}

void drawMoveCross(QPainter& p) {
    p.setPen(QPen(kControl, 7, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(50, 20), QPointF(50, 80));
    p.drawLine(QPointF(20, 50), QPointF(80, 50));
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    const QPointF heads[4][3] = {{{50, 6}, {38, 22}, {62, 22}}, {{50, 94}, {38, 78}, {62, 78}},
                                 {{6, 50}, {22, 38}, {22, 62}}, {{94, 50}, {78, 38}, {78, 62}}};
    for (const auto& h : heads) p.drawPolygon(h, 3);
}

void drawScaleSquares(QPainter& p) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kControl, 5, Qt::DashLine, Qt::RoundCap));
    p.drawRect(QRectF(14, 14, 72, 72));
    p.setPen(Qt::NoPen);
    p.setBrush(kControl);
    p.drawRect(QRectF(14, 54, 32, 32));
    p.setPen(QPen(kControl, 7, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(52, 48), QPointF(76, 24));
    p.setPen(Qt::NoPen);
    const QPointF head[] = {{84, 16}, {80, 38}, {62, 20}};
    p.drawPolygon(head, 3);
}

void drawControl(QPainter& p, ControlIcon c) {
    p.setPen(Qt::NoPen);
    switch (c) {
    case ControlIcon::Play: {
        p.setBrush(QColor(88, 200, 110));
        const QPointF tri[] = {{28, 16}, {84, 50}, {28, 84}};
        p.drawPolygon(tri, 3);
        break;
    }
    case ControlIcon::Pause:
        p.setBrush(QColor(240, 190, 80));
        p.drawRoundedRect(QRectF(24, 18, 18, 64), 4, 4);
        p.drawRoundedRect(QRectF(58, 18, 18, 64), 4, 4);
        break;
    case ControlIcon::Stop:
        p.setBrush(QColor(236, 96, 96));
        p.drawRoundedRect(QRectF(22, 22, 56, 56), 7, 7);
        break;
    case ControlIcon::ToolSelect: drawCursor(p); break;
    case ControlIcon::ToolMove: drawMoveCross(p); break;
    case ControlIcon::ToolRotate: drawCircleArrow(p); break;
    case ControlIcon::ToolScale: drawScaleSquares(p); break;
    case ControlIcon::Step: {
        p.setBrush(kControl);
        const QPointF tri[] = {{20, 18}, {66, 50}, {20, 82}};
        p.drawPolygon(tri, 3);
        p.drawRoundedRect(QRectF(70, 18, 12, 64), 3, 3);
        break;
    }
    case ControlIcon::Reset: drawCircleArrow(p); break;
    case ControlIcon::Undo: drawArrowArc(p, false); break;
    case ControlIcon::Redo: drawArrowArc(p, true); break;
    case ControlIcon::Visible: drawEye(p, true); break;
    case ControlIcon::Hidden: drawEye(p, false); break;
    case ControlIcon::Locked: drawLock(p, true); break;
    case ControlIcon::Unlocked: drawLock(p, false); break;
    case ControlIcon::Count: break;
    }
}


// ---------------------------------------------------------------------------
// Colliders: thin pale-green wireframes, the colour the viewport draws them in
// ---------------------------------------------------------------------------
const QColor kGuide(124, 255, 154);

QPen guidePen(qreal width = 3.2) { return QPen(kGuide, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }

// Every stroke twice: a dark one under a green one, so the glyph reads on light and dark buttons.
void guideStroke(QPainter& p, const std::function<void()>& draw) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 110), 5.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    draw();
    p.setPen(guidePen());
    draw();
}

void drawWireCube(QPainter& p) {
    guideStroke(p, [&p] {
        const QPointF a[] = {{22, 34}, {58, 34}, {58, 76}, {22, 76}};
        const QPointF b[] = {{42, 20}, {78, 20}, {78, 62}, {42, 62}};
        p.drawPolygon(a, 4);
        p.drawPolygon(b, 4);
        for (int k = 0; k < 4; ++k) p.drawLine(a[k], b[k]);
    });
}

void drawWireSphere(QPainter& p) {
    guideStroke(p, [&p] {
        p.drawEllipse(QPointF(50, 50), 34, 34);
        p.drawEllipse(QPointF(50, 50), 34, 12);
        p.drawEllipse(QPointF(50, 50), 12, 34);
    });
}

void drawWireCapsule(QPainter& p) {
    guideStroke(p, [&p] {
        p.drawRoundedRect(QRectF(30, 10, 40, 80), 20, 20);
        p.drawEllipse(QPointF(50, 30), 20, 6);
        p.drawEllipse(QPointF(50, 70), 20, 6);
    });
}

void drawWireHull(QPainter& p) {
    guideStroke(p, [&p] {
        const QPointF outer[] = {{50, 10}, {84, 32}, {78, 72}, {46, 90}, {16, 66}, {20, 28}};
        p.drawPolygon(outer, 6);
        p.drawLine(QPointF(50, 10), QPointF(52, 52));
        p.drawLine(QPointF(52, 52), QPointF(78, 72));
        p.drawLine(QPointF(52, 52), QPointF(16, 66));
    });
}

void drawWireParts(QPainter& p) {
    guideStroke(p, [&p] {
        p.drawRect(QRectF(14, 46, 34, 36));
        const QPointF tri[] = {{56, 82}, {88, 82}, {72, 50}};
        p.drawPolygon(tri, 3);
        p.drawEllipse(QPointF(40, 26), 16, 14);
    });
}

// Авто: the collider follows the shape - a wire cube with a small spark in its corner.
void drawWireAuto(QPainter& p) {
    drawWireCube(p);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 214, 90));
    const QPointF star[] = {{84, 64}, {88, 76}, {98, 80}, {88, 84}, {84, 96}, {80, 84}, {70, 80}, {80, 76}};
    p.drawPolygon(star, 8);
}

Draw colliderDraw(rf::ColliderKind k) {
    switch (k) {
    case rf::ColliderKind::Auto: return drawWireAuto;
    case rf::ColliderKind::Box: return drawWireCube;
    case rf::ColliderKind::Sphere: return drawWireSphere;
    case rf::ColliderKind::Capsule: return drawWireCapsule;
    case rf::ColliderKind::ConvexHull: return drawWireHull;
    case rf::ColliderKind::Decomposition: return drawWireParts;
    }
    return [](QPainter&) {};
}

// The collider as a component: a grey block inside a green wire cage.
void drawColliderRole(QPainter& p) {
    p.save();
    p.translate(50, 50);
    p.scale(0.55, 0.55);
    p.translate(-50, -50);
    drawRigidBlock(p);
    p.restore();
    guideStroke(p, [&p] { p.drawRoundedRect(QRectF(12, 12, 76, 76), 10, 10); });
}

Draw roleDraw(RoleIcon r) {
    switch (r) {
    case RoleIcon::Rigid: return drawRigidBlock;
    case RoleIcon::Soft: return drawJelly;
    case RoleIcon::Liquid: return drawDrop;
    case RoleIcon::Cloth: return drawCloth;
    case RoleIcon::Magnet: return drawMagnet;
    case RoleIcon::Smoke: return drawSmoke;
    case RoleIcon::Flame: return drawFlame;
    case RoleIcon::Heat: return drawThermometer;
    case RoleIcon::Collider: return drawColliderRole;
    case RoleIcon::Count: break;
    }
    return [](QPainter&) {};
}

Draw shapeDraw(rf::ShapeKind k) {
    const QColor c = shapeColor(k);
    switch (k) {
    case rf::ShapeKind::Box: return [c](QPainter& p) { drawCube(p, c); };
    case rf::ShapeKind::Sphere: return [c](QPainter& p) { drawSphere(p, c); };
    case rf::ShapeKind::Cylinder: return [c](QPainter& p) { drawCylinder(p, c); };
    case rf::ShapeKind::Cone: return [c](QPainter& p) { drawCone(p, c); };
    case rf::ShapeKind::Plane: return [c](QPainter& p) { drawPlane(p, c); };
    case rf::ShapeKind::Mesh: return [c](QPainter& p) { drawModel(p, c); };
    }
    return [](QPainter&) {};
}

// ---------------------------------------------------------------------------
// Many objects: a group and the three patterns of an array
// ---------------------------------------------------------------------------
// A small shaded cube with its centre at (x, y), `size` wide (the big cube scaled down).
void smallCube(QPainter& p, qreal x, qreal y, qreal size, const QColor& c) {
    p.save();
    p.translate(x - size / 2, y - size / 2);
    p.scale(size / 100.0, size / 100.0);
    drawCube(p, c);
    p.restore();
}

void drawObjectIcon(QPainter& p, ObjectIcon kind) {
    const QColor c(236, 150, 88);
    switch (kind) {
    case ObjectIcon::Group: // three cubes and a dashed frame around them
        p.setPen(QPen(QColor(222, 226, 234), 3, Qt::DashLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(8, 12, 84, 78), 10, 10);
        smallCube(p, 34, 64, 40, QColor(90, 150, 234));
        smallCube(p, 66, 64, 40, QColor(110, 196, 98));
        smallCube(p, 50, 36, 40, c);
        break;
    case ObjectIcon::ArrayLine:
        for (int i = 0; i < 4; ++i) smallCube(p, 14 + 24 * i, 52, 30, c);
        break;
    case ObjectIcon::ArrayGrid:
        for (int i = 0; i < 9; ++i) smallCube(p, 22 + 28 * (i % 3), 22 + 28 * (i / 3), 26, c);
        break;
    case ObjectIcon::ArrayCircle:
        for (int i = 0; i < 8; ++i) {
            const qreal a = 6.2831853 * i / 8;
            smallCube(p, 50 + 34 * std::cos(a), 50 + 34 * std::sin(a), 22, c);
        }
        break;
    case ObjectIcon::Count: break;
    }
}

// ---------------------------------------------------------------------------
// Lights and cameras
// ---------------------------------------------------------------------------
// The sun: a warm disc and eight rays.
void drawSun(QPainter& p) {
    p.setPen(QPen(QColor(255, 196, 64), 7, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 8; ++i) {
        const qreal a = 6.2831853 * i / 8, c = std::cos(a), s = std::sin(a);
        p.drawLine(QPointF(50 + 31 * c, 50 + 31 * s), QPointF(50 + 44 * c, 50 + 44 * s));
    }
    QRadialGradient g(QPointF(43, 43), 30);
    g.setColorAt(0, QColor(255, 247, 190));
    g.setColorAt(1, QColor(250, 170, 40));
    p.setPen(outline(QColor(250, 170, 40)));
    p.setBrush(g);
    p.drawEllipse(QPointF(50, 50), 22, 22);
}

// A lamp: a glowing glass bulb on a threaded base.
void drawBulb(QPainter& p) {
    QRadialGradient glow(QPointF(50, 40), 46);
    glow.setColorAt(0, QColor(255, 230, 120, 110));
    glow.setColorAt(1, QColor(255, 230, 120, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(QPointF(50, 40), 46, 46);
    QRadialGradient glass(QPointF(42, 30), 34);
    glass.setColorAt(0, QColor(255, 255, 235));
    glass.setColorAt(1, QColor(255, 206, 80));
    QPainterPath bulb;
    bulb.moveTo(38, 66);
    bulb.cubicTo(38, 56, 22, 50, 22, 36);
    bulb.cubicTo(22, 20, 36, 10, 50, 10);
    bulb.cubicTo(64, 10, 78, 20, 78, 36);
    bulb.cubicTo(78, 50, 62, 56, 62, 66);
    bulb.closeSubpath();
    p.setPen(outline(QColor(230, 170, 60)));
    p.setBrush(glass);
    p.drawPath(bulb);
    p.setBrush(QColor(150, 158, 172));
    p.setPen(outline(QColor(150, 158, 172)));
    p.drawRoundedRect(QRectF(37, 66, 26, 20), 4, 4);
    p.drawLine(QPointF(38, 73), QPointF(62, 73));
    p.drawLine(QPointF(38, 80), QPointF(62, 80));
    p.drawEllipse(QPointF(50, 89), 5, 3);
}

// A spotlight: a dark lamp head at the top left, its cone of light falling to a bright ellipse.
void drawSpotlight(QPainter& p) {
    QPainterPath cone;
    cone.moveTo(30, 30);
    cone.lineTo(14, 84);
    cone.arcTo(QRectF(14, 74, 76, 20), 180, 180);
    cone.lineTo(46, 22);
    cone.closeSubpath();
    QLinearGradient light(QPointF(38, 26), QPointF(52, 90));
    light.setColorAt(0, QColor(255, 226, 120, 220));
    light.setColorAt(1, QColor(255, 226, 120, 60));
    p.setPen(Qt::NoPen);
    p.setBrush(light);
    p.drawPath(cone);
    p.setBrush(QColor(255, 240, 170, 200));
    p.drawEllipse(QRectF(14, 74, 76, 20));
    const QPointF head[] = {{24, 8}, {50, 2}, {54, 24}, {26, 34}};
    p.setPen(outline(QColor(120, 128, 142)));
    p.setBrush(QColor(120, 128, 142));
    p.drawPolygon(head, 4);
}

// A camera: a body, two film reels on top, a lens in front.
void drawCameraIcon(QPainter& p) {
    const QColor body(98, 108, 124), dark(62, 68, 80);
    p.setPen(outline(body));
    p.setBrush(dark);
    p.drawEllipse(QPointF(30, 26), 14, 14);
    p.drawEllipse(QPointF(58, 26), 14, 14);
    QLinearGradient g(QPointF(0, 40), QPointF(0, 82));
    g.setColorAt(0, shade(body, 130));
    g.setColorAt(1, shade(body, 75));
    p.setBrush(g);
    p.drawRoundedRect(QRectF(10, 40, 62, 42), 7, 7);
    const QPointF lens[] = {{72, 52}, {94, 40}, {94, 82}, {72, 70}};
    p.setBrush(dark);
    p.drawPolygon(lens, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(120, 190, 255));
    p.drawEllipse(QPointF(22, 51), 4, 4);
}

void drawSceneIcon(QPainter& p, SceneIcon kind) {
    switch (kind) {
    case SceneIcon::Sun: drawSun(p); break;
    case SceneIcon::Bulb: drawBulb(p); break;
    case SceneIcon::Spot: drawSpotlight(p); break;
    case SceneIcon::Camera: drawCameraIcon(p); break;
    case SceneIcon::Count: break;
    }
}

} // namespace

QIcon sceneIcon(SceneIcon kind, int size) {
    return makeIcon(size, [kind](QPainter& p) { drawSceneIcon(p, kind); });
}

QIcon objectIcon(ObjectIcon kind, int size) {
    return makeIcon(size, [kind](QPainter& p) { drawObjectIcon(p, kind); });
}

QIcon shapeIcon(rf::ShapeKind shape, int size) { return makeIcon(size, shapeDraw(shape)); }
QIcon roleIcon(RoleIcon role, int size) { return makeIcon(size, roleDraw(role)); }
QIcon controlIcon(ControlIcon control, int size) {
    return makeIcon(size, [control](QPainter& p) { drawControl(p, control); });
}
QPixmap rolePixmap(RoleIcon role, int size) { return render(size, 2.0, roleDraw(role)); }
QIcon colliderIcon(rf::ColliderKind kind, int size) { return makeIcon(size, colliderDraw(kind)); }

int dumpIcons(const QString& dir, int size) {
    QDir().mkpath(dir);
    int n = 0;
    auto save = [&](const QString& name, const Draw& draw) { n += render(size, 1.0, draw).save(dir + "/" + name + ".png") ? 1 : 0; };
    const char* shapes[] = {"shape-cube", "shape-sphere", "shape-cylinder", "shape-cone", "shape-plane", "shape-model"};
    for (int k = 0; k < 6; ++k) save(shapes[k], shapeDraw(rf::ShapeKind(k)));
    const char* roles[] = {"role-rigid", "role-soft", "role-liquid", "role-cloth", "role-magnet", "role-smoke", "role-flame", "role-heat", "role-collider"};
    for (int k = 0; k < int(RoleIcon::Count); ++k) save(roles[k], roleDraw(RoleIcon(k)));
    const char* colliders[] = {"collider-auto", "collider-box", "collider-sphere", "collider-capsule", "collider-hull", "collider-parts"};
    for (int k = 0; k < 6; ++k) save(colliders[k], colliderDraw(rf::ColliderKind(k)));
    const char* controls[] = {"play", "pause", "stop", "step", "reset", "undo", "redo", "visible", "hidden", "locked", "unlocked",
                              "tool-select", "tool-move", "tool-rotate", "tool-scale"};
    for (int k = 0; k < int(ControlIcon::Count); ++k) {
        const ControlIcon c = ControlIcon(k);
        save(controls[k], [c](QPainter& p) { drawControl(p, c); });
    }
    const char* objects[] = {"group", "array-line", "array-grid", "array-circle"};
    for (int k = 0; k < int(ObjectIcon::Count); ++k) {
        const ObjectIcon o = ObjectIcon(k);
        save(objects[k], [o](QPainter& p) { drawObjectIcon(p, o); });
    }
    const char* lights[] = {"light-sun", "light-bulb", "light-spot", "camera"};
    for (int k = 0; k < int(SceneIcon::Count); ++k) {
        const SceneIcon o = SceneIcon(k);
        save(lights[k], [o](QPainter& p) { drawSceneIcon(p, o); });
    }
    return n;
}
