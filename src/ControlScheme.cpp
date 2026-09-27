// The control schemes (see ControlScheme.h): one table per scheme, Alt + a button the same in all.
#include "ControlScheme.h"

#include <QSettings>

CameraMove cameraMoveFor(ControlScheme s, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
    if (modifiers & Qt::AltModifier) { // Maya / Unity / Unreal / Houdini
        if (button == Qt::LeftButton) return CameraMove::Orbit;
        if (button == Qt::MiddleButton) return CameraMove::Pan;
        if (button == Qt::RightButton) return CameraMove::Dolly;
        return CameraMove::None;
    }
    switch (s) {
    case ControlScheme::Simple:
        if (button == Qt::MiddleButton) return CameraMove::Pan;
        if (button == Qt::RightButton) return CameraMove::Orbit;
        break;
    case ControlScheme::Blender:
        if (button == Qt::MiddleButton) {
            if (modifiers & Qt::ShiftModifier) return CameraMove::Pan;
            if (modifiers & Qt::ControlModifier) return CameraMove::Dolly;
            return CameraMove::Orbit;
        }
        break;
    case ControlScheme::MayaUnity:
        if (button == Qt::MiddleButton) return CameraMove::Pan;
        if (button == Qt::RightButton) return CameraMove::Look;
        break;
    }
    return CameraMove::None;
}

bool rightButtonFlies(ControlScheme s) { return s != ControlScheme::Blender; }

bool blenderKeys(ControlScheme s) { return s == ControlScheme::Blender; }

QString controlSchemeName(ControlScheme s) {
    switch (s) {
    case ControlScheme::Simple: return "Простая";
    case ControlScheme::Blender: return "Как в Blender";
    case ControlScheme::MayaUnity: return "Как в Maya/Unity";
    }
    return {};
}

QString cameraHint(ControlScheme s) {
    switch (s) {
    case ControlScheme::Simple: return "ПКМ: осмотр (клик — меню, +WASD — полёт) · СКМ: панорама · колесо: зум";
    case ControlScheme::Blender: return "СКМ: вращать · Shift+СКМ: панорама · Ctrl+СКМ, колесо: зум · ПКМ: меню";
    case ControlScheme::MayaUnity: return "Alt+ЛКМ: вращать · СКМ: панорама · Alt+ПКМ, колесо: зум · ПКМ: осмотр, +WASD — полёт";
    }
    return {};
}

ControlScheme savedControlScheme() {
    const int v = QSettings().value("controls/scheme", 0).toInt();
    return v >= 0 && v <= 2 ? ControlScheme(v) : ControlScheme::Simple;
}

void saveControlScheme(ControlScheme s) { QSettings().setValue("controls/scheme", int(s)); }
