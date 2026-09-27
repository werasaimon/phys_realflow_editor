#pragma once
// Which mouse button moves the camera, and how - the three schemes of docs/controls.md.
//   Простая (the default): the left button never moves the camera - it selects, draws a selection
//     box from empty space and drags the gizmo; the right button dragged orbits around the point
//     under the cursor, clicked opens the menu, held with W A S D (Q / E down / up, Shift faster)
//     flies; the middle button pans; the wheel zooms towards the cursor.
//   Как в Blender: the middle button orbits, Shift+middle pans, Ctrl+middle zooms; G / R / S move,
//     turn and scale from the keyboard, X deletes, Shift+D duplicates.
//   Как в Maya/Unity: the camera through Alt + a button; the right button looks around in place and
//     flies with W A S D, as in Unity and Unreal.
// In every scheme Alt+left orbits, Alt+middle pans and Alt+right zooms (Maya, Unity, Unreal and
// Houdini all agree on that), so a hand trained anywhere works at once.
#include <QString>
#include <Qt>

enum class ControlScheme { Simple, Blender, MayaUnity };
enum class CameraMove { None, Orbit, Pan, Dolly, Look };

// What a drag with this button (and these keys held) does to the camera; None: the button belongs to
// the tools (selection, the gizmo, grabbing a body).
CameraMove cameraMoveFor(ControlScheme s, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
// Holding the right button and pressing W A S D flies (Простая, Как в Maya/Unity).
bool rightButtonFlies(ControlScheme s);
// G / R / S keyboard transforms, X deletes, Shift+D duplicates: only "Как в Blender".
bool blenderKeys(ControlScheme s);
QString controlSchemeName(ControlScheme s); // "Простая", "Как в Blender", "Как в Maya/Unity"
// The camera part of the status line: "ПКМ: осмотр (клик — меню) · СКМ: панорама · колесо: зум".
QString cameraHint(ControlScheme s);

// The scheme the user chose, kept in QSettings ("controls/scheme"); Простая when never chosen.
ControlScheme savedControlScheme();
void saveControlScheme(ControlScheme s);
