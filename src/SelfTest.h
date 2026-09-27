#pragma once
// A self-test of the scene builder in the real window (the command line's --self-test). It checks
// the gizmo's arithmetic with numbers (a drag of an arrow keeps the grabbed point under the cursor,
// a quarter turn of a ring is 90 degrees, snapping rounds, Euler angles survive the round trip),
// then presses the same buttons and drags the same handles a person does - create a cube (geometry
// only: it stays put), give it a role, play (it falls), stop (it is back), drag its X arrow with the
// mouse, click three times on three objects in a row (each click takes the next one behind) - and
// checks the scene graph after each step. Returns the number of failures.
class QMainWindow;
class QString;

int runBuilderSelfTest(QMainWindow& window, const QString& shotsDir);
// Screenshots of the gizmo on a selected cube, one axis under the mouse: gizmo-w.png (move),
// gizmo-e.png (rotate), gizmo-r.png (scale), gizmo-e-drag.png (a turn in progress), and close-ups
// (*-close.png). Returns the number of pictures that could not be taken.
int runGizmoShots(QMainWindow& window, const QString& dir);
