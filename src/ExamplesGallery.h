#pragma once
// "Примеры": every ready-made scene as a tile with a picture - the editor's own scenes (examples/,
// they can be changed) and the SDK's samples (water, fire, plasma, lessons) - filtered by kind.
// A click opens it.
//
// The pictures are made by the editor itself, one at a time, in the background: it starts its own
// program without a window (--thumbnail), lets the scene run a moment, saves the view and quits.
// They are kept in the user's cache folder (QStandardPaths::CacheLocation/thumbnails), so this
// happens once; until a picture is ready its tile shows a placeholder. The helper runs at a lower
// priority so the editor stays smooth.
#include <QDialog>

#include <deque>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QProcess;
class QTabBar;
class QTimer;

class ExamplesGallery : public QDialog {
    Q_OBJECT
public:
    struct Example {
        QString title;
        QString category;
        QString sceneFile; // an editor scene (*.rfscene), or empty for an SDK sample
        int preset = -1;   // the SDK sample's number
    };

    ExamplesGallery(const QString& examplesDir, QWidget* parent = nullptr);
    ~ExamplesGallery() override;
    const Example* chosen() const { return chosen_ >= 0 ? &examples_[size_t(chosen_)] : nullptr; }
    int pendingThumbnails() const { return int(queue_.size()) + (running_ >= 0 ? 1 : 0); }
    static QString thumbnailDir();

signals:
    void thumbnailsDone();

private:
    void collect(const QString& examplesDir);
    void buildTiles();
    void filter(int tab);
    QString thumbnailPath(const Example& e) const;
    QStringList thumbnailArguments(const Example& e, const QString& out) const;
    void startNext();              // the next missing picture, if none is being made
    void finishRunning(bool saved);

    std::vector<Example> examples_;
    std::vector<QListWidgetItem*> items_;
    QListWidget* list_ = nullptr;
    QTabBar* tabs_ = nullptr;
    QStringList categories_;
    std::deque<int> queue_;    // examples whose picture is still to be made
    int running_ = -1;         // the one being made now
    QProcess* process_ = nullptr;
    QTimer* watchdog_ = nullptr; // a scene that takes too long keeps its placeholder
    int chosen_ = -1;
};
