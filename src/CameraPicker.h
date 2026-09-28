#pragma once
// "Смотрю через: [Вид редактора ▾]" - a small list in the top-left corner of the 3D view that says
// through whose eyes the view looks, and lets you change it in one click.
//
// Why here, on the view itself: the first user to add cameras could not find how to look through
// one - the button sat deep in the inspector, the menu under "Вид → Камеры". A choice about the
// picture belongs on the picture, where the eyes already are: Blender, Unreal and Unity all put
// their camera / viewpoint switch in the corner of the viewport.
//
// What it shows: "Вид редактора" first (the editor's own free camera), then every camera of the
// scene by name. It follows the scene by itself: a camera added, renamed or removed, or the view
// switched another way (the inspector's button, the menu, Esc), and the list shows it at once.
// Choosing an entry asks the owner to look through that camera (the `chosen` signal).
#include <QObject>
#include <QString>

#include <cstdint>
#include <vector>

class QComboBox;
class QFrame;
class QWidget;

class CameraPicker : public QObject {
    Q_OBJECT
public:
    // One line of the list: a camera's id and name. Id 0 is the editor's own view.
    struct Choice {
        uint32_t cameraId = 0;
        QString name;
        bool operator==(const Choice& o) const { return cameraId == o.cameraId && name == o.name; }
    };

    // The list lives on `view` (a child widget in its top-left corner, under the scene's name).
    explicit CameraPicker(QWidget* view);

    // The scene's cameras and the one the view looks through now (0: the editor's view). The list
    // is rebuilt only when the cameras changed, so it can be called on every frame of a camera move.
    void showCameras(const std::vector<Choice>& cameras, uint32_t current);

    QComboBox* list() const { return list_; }
    QFrame* frame() const { return frame_; }

signals:
    void chosen(uint32_t cameraId); // the user picked this entry (0: the editor's view)

private:
    QFrame* frame_ = nullptr;
    QComboBox* list_ = nullptr;
    std::vector<Choice> shown_; // what the list holds now, "Вид редактора" not included
    bool filling_ = false;      // true while the list is being refilled: no `chosen` then
};
