#pragma once
// How the viewer shades a mesh: "auto smooth", as Blender's Auto Smooth or 3ds Max's smoothing
// groups. Every corner of a triangle gets the mean normal of the faces around its point that bend
// less than the crease angle from its own face; sharper edges stay sharp. So a sphere is round, a
// cylinder is round on its side with flat caps, a cube keeps its edges and a low-poly model keeps
// its facets. The same rule in edit mode and in play: the object looks the same in both.
#include "core/Mesh.h"
#include "math/Vector3.h"

#include <vector>

// One normal per triangle corner: normals[3 * t + k] for corner k of triangle t. Points at the same
// place count as one point, so a seam where a mesh repeats its vertices is smoothed across too.
std::vector<rf::Vector3> autoSmoothNormals(const rf::TriMesh& mesh, float creaseDegrees = 35.0f);
