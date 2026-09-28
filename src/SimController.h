#pragma once
// Runs the Simulation on a background thread. The GUI never touches the Simulation directly:
// it posts commands (executed between frames on the simulation thread) and reads immutable
// RenderSnapshots published after every frame. The newest one is what the view draws; every one of
// them also waits in a short queue (takePublished) so the Laboratory's recording misses no frame even
// when the window paints slower than the simulation steps. And every stepped frame leaves its numbers
// - without its picture - in a longer queue (takeReadings): the plots record every frame the
// simulation made, however slowly the window paints (a research plot never skips frames).

#include "scene/Simulation.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class SimController {
public:
    using Command = std::function<void(rf::Simulation&)>;

    SimController();
    ~SimController();

    void post(Command cmd);
    void setRunning(bool on);
    bool isRunning() const { return running_; }
    void requestStep();
    void setRealtimeLimit(bool on) { realtime_ = on; }

    std::shared_ptr<const rf::RenderSnapshot> snapshot() const;
    uint64_t serial() const { return serial_; }
    // Every snapshot published since the last call, oldest first (at most kPublishedKept: a window that
    // does not ask for a long while loses the oldest, never grows without bound).
    std::vector<std::shared_ptr<const rf::RenderSnapshot>> takePublished();
    static constexpr size_t kPublishedKept = 240;

    // One stepped frame's numbers: what the plots record.
    struct Reading {
        uint64_t frame = 0;
        double time = 0;
        std::string scene;
        std::vector<std::pair<std::string, float>> plots; // the solvers' own readings
        std::vector<rf::Probe::Channel> channels;         // the Probe's channels
        std::vector<rf::Measurement> measurements;        // the SDK's physical channels, as asked for
    };
    // Every stepped frame's numbers since the last call, oldest first (at most kReadingsKept: about
    // 5 minutes at 60 frames a second if nobody asks, never more).
    std::vector<Reading> takeReadings();
    static constexpr size_t kReadingsKept = 20000;

private:
    void loop();
    void publish(bool stepped);

    std::unique_ptr<rf::Simulation> sim_;
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Command> queue_;
    int stepRequests_ = 0;
    std::atomic<bool> running_{false}, quit_{false}, realtime_{true};

    mutable std::mutex snapMtx_;
    std::shared_ptr<const rf::RenderSnapshot> snap_;
    std::deque<std::shared_ptr<const rf::RenderSnapshot>> published_; // not yet taken (under snapMtx_)
    std::deque<Reading> readings_;                                      // not yet taken (under snapMtx_)
    std::atomic<uint64_t> serial_{0};
};
