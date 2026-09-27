#pragma once
// The collider of a rigid body drawn as thin guide lines over its geometry, as Houdini draws the
// collision guide of a packed RBD object: the user sees WHAT the body collides with (a barrel
// around a sphere) while the geometry itself stays what it is. Lines, not a triangle wireframe:
// a box gets its 12 edges, a sphere three great circles, a capsule its two rings, four side lines
// and the arcs of its caps, a convex hull (and every part of a convex decomposition) its real edges
// - the diagonals that only split a flat face into triangles are left out.
//
// The lines of one shape are made once in the shape's own frame and kept while the shape lives;
// every frame they are only turned and moved to where the body is (play mode) or where the object
// stands (edit mode).
#include "math/Math.h"
#include "rigid/Shapes.h"

#include <map>
#include <memory>
#include <vector>

class ColliderGuides {
public:
    // What the collider belongs to, for its colour: a body that moves (green), a fixed obstacle
    // (grey), a body at rest that the solver has put to sleep (dim green).
    enum State { Dynamic = 0, Static = 1, Sleeping = 2 };
    // Appends the guide lines of `shape` placed at (position, rotation) as pairs of points,
    // x y z state per point (GL_LINES, the viewport's line layout; the state picks the colour).
    void append(const std::shared_ptr<const rf::ConvexShape>& shape, const rf::Vector3& position,
                const rf::Quaternion& rotation, State state, std::vector<float>& lines);
    // Forgets the lines of shapes nobody else holds any more (call once per rebuild).
    void prune();
    // The lines of a shape in its own frame (tests; also what append() places).
    const std::vector<rf::Vector3>& localLines(const std::shared_ptr<const rf::ConvexShape>& shape);

private:
    struct Entry {
        std::shared_ptr<const rf::ConvexShape> shape; // held so its address cannot be reused while cached
        std::vector<rf::Vector3> lines;               // segment end points, pairs, in the shape's frame
    };
    std::map<const rf::ConvexShape*, Entry> cache_;
};

// The guide lines of one shape kind in the shape's frame (pairs of points).
std::vector<rf::Vector3> boxGuide(const rf::Vector3& halfExtents);
std::vector<rf::Vector3> sphereGuide(float radius);
std::vector<rf::Vector3> capsuleGuide(float radius, float halfHeight); // axis along y
std::vector<rf::Vector3> meshEdgeGuide(const rf::TriMesh& mesh);       // edges between faces that bend
