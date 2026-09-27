#pragma once
// The navigation cube in the top-right corner of the 3D view, a hybrid of Blender's navigation gizmo
// and Unity's orientation cube: a small shaded cube turned as the scene is, with six balls around it
// on the axes (X red, Y green, Z blue; the positive ones filled and lettered, the negative ones
// hollow). A click on a ball looks along that axis at the scene (the ball of +Y: from the top); a
// drag anywhere on it orbits the view. No keyboard and no numpad needed.
//
// Only arithmetic and QPainter: where things are on screen for a given view, and what is under a
// pixel - so the self-test can click it by numbers.
#include <QMatrix4x4>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QVector3D>

class QPainter;

class NavCube {
public:
    // The widget's square in the viewport's pixels.
    QRectF rect(const QSize& viewport) const;
    // What is under a pixel: a ball 0..5 (+X, -X, +Y, -Y, +Z, -Z), kInside (the disc: a drag
    // orbits), or kOutside.
    static constexpr int kInside = -1, kOutside = -2;
    int hit(const QPointF& p, const QMatrix4x4& view, const QSize& viewport) const;
    // The world direction of ball k (+X, -X, +Y, -Y, +Z, -Z).
    static QVector3D axis(int k);
    // Where ball k is drawn for this view.
    QPointF ballCentre(int k, const QMatrix4x4& view, const QSize& viewport) const;
    void draw(QPainter& p, const QMatrix4x4& view, const QSize& viewport, int hover) const;

private:
    void drawCube(QPainter& p, const QMatrix4x4& view, const QPointF& centre) const;
    static constexpr float kSize = 116.0f, kMargin = 14.0f, kBall = 10.0f;
};
