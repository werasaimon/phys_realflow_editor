#pragma once
// Lights and cameras in the 3D view. They have no geometry, so the editor draws each as a small
// wireframe (the way Blender and Maya do) and the mouse picks it by a ball around that wireframe:
//   Солнце    - a disc across its direction and an arrow along it (sunlight is parallel: where the
//               sun stands does not matter, only where it points);
//   Лампа     - a bulb of three rings; when selected, a faint circle as wide as its range;
//   Прожектор - its cone: the full cone solid, the cone where the soft edge begins faint;
//   Камера    - what it sees as a pyramid (the eye at the tip, the frame 0.35 m in front), with a
//               triangle over the frame's top edge that says which way is up.
// The builder makes the markers from the scene graph; the viewport draws them and the builder picks
// them. Lines are pairs of points in the world, x y z 0 each (the viewport's line format).
#include "OrbitCamera.h"
#include "math/Math.h"

#include <cstdint>
#include <vector>

struct SceneMarker {
    enum Kind { Sun, Point, Spot, Camera };
    uint32_t id = 0;
    Kind kind = Point;
    rf::Vector3 position;    // in the world
    rf::Quaternion rotation; // in the world: a light shines along -y, a camera looks along -z
    rf::Vector3 color{1.0f};
    float range = 5, coneDeg = 40, softnessDeg = 10, fovDeg = 50;
    bool selected = false;
};

// The marker's wireframe: solid lines, and the faint ones (a range, a soft edge) apart. `aspect` is
// the width / height of the view a camera's frame is drawn with.
void markerLines(const SceneMarker& m, float aspect, std::vector<float>& solid, std::vector<float>& faint);
// Where the ray meets the ball around the marker (t along the ray); false: it misses, or it starts
// inside the ball (a camera just made where the view stands must not catch every click).
bool markerHit(const SceneMarker& m, const Ray& ray, float& t);
