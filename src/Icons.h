#pragma once
// Icons of the editor, drawn in code with QPainter: no image files, one consistent look (soft light
// from the top left, a thin darker outline, transparent background), crisp at any screen scale
// (each icon holds a 1x and a 2x pixmap). The shapes are shaded like small 3D objects so a beginner
// recognises "cube" and "sphere" at a glance; each role has a picture of what it turns a shape into.
#include "scene/SceneGraph.h"

#include <QIcon>
#include <QPixmap>
#include <QString>

// The components as the editor shows them: what a shape is made of (the first four, one at a
// time), what it also does, and its collider (what it collides with - a component of its own, as
// in Unity: alone it is a fixed obstacle, with Rigid a moving body).
enum class RoleIcon { Rigid, Soft, Liquid, Cloth, Magnet, Smoke, Flame, Heat, Collider, Count };

// Buttons of the simulation: run, pause, stop, one step, back to the start, undo, redo, eye, lock,
// and the tools of the edit mode: select, move, rotate, scale.
enum class ControlIcon {
    Play, Pause, Stop, Step, Reset, Undo, Redo, Visible, Hidden, Locked, Unlocked,
    ToolSelect, ToolMove, ToolRotate, ToolScale, Count
};

QIcon shapeIcon(rf::ShapeKind shape, int size = 48);
QIcon roleIcon(RoleIcon role, int size = 40);
QIcon controlIcon(ControlIcon control, int size = 32);
QPixmap rolePixmap(RoleIcon role, int size); // for small chips next to a name
// The kinds of collider as small wireframe glyphs: Авто, Коробка, Сфера, Капсула, Оболочка, Точно.
QIcon colliderIcon(rf::ColliderKind kind, int size = 28);

// Writes every icon as a PNG into `dir` (for the README and a quick look); returns the file count.
int dumpIcons(const QString& dir, int size = 96);
