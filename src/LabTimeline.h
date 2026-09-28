#pragma once
// The Laboratory's timeline: the last frames of the simulation kept in memory, and a scrubber at the
// bottom of the 3D view to look at any of them again - the way PhysX PVD replays a recording.
//
// What is kept is the frame exactly as the view drew it (the immutable rf::RenderSnapshot the
// simulation thread published), so going back shows that frame bit for bit; nothing is simulated
// again in part 1. The ring holds at most `capacity` frames (600 = 10 s at 60 frames a second) and
// at most kBudgetBytes of memory: a snapshot of 200 boxes is a few tens of kilobytes, one of a smoke
// box with its volume half a megabyte, so a heavy scene keeps fewer seconds, never more memory. The
// strip says how much it holds.
//
// Taking the scrubber pauses the scene (a moving recording under the cursor would be useless);
// «Вживую» shows the newest frame again and lets the scene run on if it was running (MainWindow
// wires both through the scrubStarted and wentLive signals).
#include "scene/Simulation.h"

#include <QFrame>

#include <deque>
#include <memory>

class QLabel;
class QPushButton;
class QSlider;
class QToolButton;

class LabTimeline : public QFrame {
    Q_OBJECT
public:
    using Frame = std::shared_ptr<const rf::RenderSnapshot>;
    static constexpr size_t kBudgetBytes = size_t(512) << 20; // 512 MB at most, whatever the capacity

    // The strip lives on the view (its child), stretched along its bottom edge.
    explicit LabTimeline(QWidget* view);

    // A frame the simulation published: kept unless it is the same frame again; a frame number that
    // went back (a reset, a new play) starts the recording over.
    void record(const Frame& s);
    void clear();
    void setCapacity(int frames);
    int capacity() const { return capacity_; }

    int count() const { return int(frames_.size()); }
    size_t bytes() const { return bytes_; }
    bool showingPast() const { return shown_ >= 0; }
    int position() const { return shown_ >= 0 ? shown_ : count() - 1; } // 0 = the oldest kept frame
    Frame frameAt(int index) const;
    Frame newest() const { return frames_.empty() ? nullptr : frames_.back(); }

    void seek(int index); // show kept frame `index` (the scrubber, ◀ ▶, the self-test)
    void goLive();        // the newest frame again («Вживую»)

    // How much memory a snapshot holds, roughly: its big arrays (bodies, particles, the smoke volume,
    // the slice, the debug drawing). Shared meshes are not counted - every frame points at the same ones.
    static size_t approxBytes(const rf::RenderSnapshot& s);

signals:
    void showFrame(Frame s);   // the view should draw this kept frame
    void scrubStarted();       // the user took the scrubber (or ◀ ▶): the scene should pause
    void wentLive();           // back to the newest frame: the scene may run on
    void placed(int height);   // the strip was laid out: its height, for what sits above it

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(class QShowEvent* e) override;
    void hideEvent(class QHideEvent* e) override;

private:
    void place();          // along the bottom of the view, 12 px from its edges
    void refresh();        // the slider's range and position, the words
    void step(int delta);  // ◀ ▶: one frame back or forward
    void dropOldest();

    QWidget* view_;
    std::deque<Frame> frames_;
    std::deque<size_t> sizes_;
    size_t bytes_ = 0;
    int capacity_ = 600;
    int shown_ = -1; // the kept frame shown (-1: live)
    QToolButton* back_ = nullptr;
    QToolButton* forward_ = nullptr;
    QSlider* slider_ = nullptr;
    QLabel* label_ = nullptr;
    QPushButton* live_ = nullptr;
};
