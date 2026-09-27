#pragma once
// A self-test of the scene builder in the real window (the command line's --self-test): it presses
// the same buttons a person presses - create a cube, make it a magnet, give it smoke, turn it into
// cloth, undo, redo - and checks the scene graph after each click. Returns the number of failures.
class QMainWindow;

int runBuilderSelfTest(QMainWindow& window);
