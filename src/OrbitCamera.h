#pragma once
// The camera: an eye looking at a target point from yaw / pitch at a distance, and the screen-to-world
// ray under a pixel. Besides the plain orbit around its target it turns around any point (the one
// under the cursor, as Blender's "orbit around selection" and Maya's tumble on a point: that point
// stays where it is on screen), zooms towards the point under the cursor, flies (the eye and the
// target move together) and looks along an axis (the navigation cube's views).

#include "math/Math.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>

struct Ray {
    rf::Vector3 origin;
    rf::Vector3 dir; // unit
    rf::Vector3 at(float t) const { return origin + dir * t; }
};

class OrbitCamera {
public:
    QMatrix4x4 view() const;
    QMatrix4x4 projection(float aspect) const;
    QVector3D eye() const;
    QVector3D forward() const { return (target_ - eye()).normalized(); }
    float fovDeg() const { return fov_; }
    float distance() const { return distance_; }

    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    QVector3D target() const { return target_; }

    void rotate(float dxPixels, float dyPixels);
    // Turns the eye around `pivot` by the same angles as rotate(): the view turns with it, so the
    // pivot keeps its place on screen. Around the eye itself it is looking about (flying).
    void orbitAround(const QVector3D& pivot, float dxPixels, float dyPixels);
    void pan(float dxPixels, float dyPixels);
    void zoom(float wheelSteps);
    // Zoom by wheel steps towards `point` (the point under the cursor): it stays under the cursor.
    void zoomToward(const QVector3D& point, float wheelSteps);
    // Moves the eye and the target together (flying), in world units.
    void translate(const QVector3D& delta) { target_ += delta; }
    // Looks along a direction (the view's forward), keeping the target: the axis views.
    void lookAlong(const QVector3D& forward);
    QVector3D right() const; // the screen's right in the world
    // Fit the whole box in view; yaw/pitch in degrees.
    void frame(const rf::AABB& box, float yawDeg, float pitchDeg);

    // Ray through a pixel (logical coordinates, origin top-left): the inverse view-projection
    // maps the pixel at NDC depth -1 (near plane) and +1 (far plane) back to world space.
    Ray screenRay(const QPointF& pixel, const QSize& viewport) const;

private:
    QVector3D target_{0, 0.5f, 0};
    float distance_ = 3.5f, yaw_ = 55.0f, pitch_ = 22.0f, fov_ = 40.0f;
};

// Ray / axis-aligned plane intersection: plane {p : p[axis] == coord}. Returns t or -1.
float intersectAxisPlane(const Ray& r, int axis, float coord);
// Ray / box: entry and exit distances (tNear clamped to 0). False if missed.
bool intersectBox(const Ray& r, const rf::AABB& b, float& tNear, float& tFar);
