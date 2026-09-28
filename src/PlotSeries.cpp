// One channel's bounded history (see PlotSeries.h).
#include "PlotSeries.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr size_t kRecent = 36000;      // a kept channel: its last samples one by one (10 minutes at 60 Hz)
constexpr size_t kMost = 60000;        // a kept channel: at most this many in all
constexpr double kShortSeconds = 60.0; // any other channel: the last minute
} // namespace

void PlotSeries::add(double t, double v) {
    if (!std::isfinite(v) && (size() == 0 || !std::isfinite(v_.back()))) return; // one gap mark is enough
    t_.push_back(t);
    v_.push_back(v);
    trim();
}

void PlotSeries::setKeep(bool wholeRun) { keep_ = wholeRun; }

void PlotSeries::clear() {
    t_.clear();
    v_.clear();
    begin_ = 0;
}

// The bounds, after every sample:
//   a channel kept for the whole run - past 60 000 samples, the ones before its last 36 000 are
//   thinned by half (so the vector never grows past 65 536: its memory is bounded for good);
//   any other channel - the samples older than a minute go (the front of the vector is let go in
//   large pieces, so that dropping costs nothing per frame).
void PlotSeries::trim() {
    const double now = t_.back();
    if (keep_) {
        if (size() > kMost) thinOld(begin_ + size() - kRecent);
        return;
    }
    while (begin_ + 1 < t_.size() && t_[begin_] < now - kShortSeconds) ++begin_;
    if (begin_ > 4096 && begin_ * 2 > t_.size()) {
        t_.erase(t_.begin(), t_.begin() + std::ptrdiff_t(begin_));
        v_.erase(v_.begin(), v_.begin() + std::ptrdiff_t(begin_));
        begin_ = 0;
    }
}

// The samples before `end`, 4 at a time, become 2: the lowest and the highest, in the order they came.
// A group with a gap in it stays as it is (gaps are rare: a channel that stopped being measured).
void PlotSeries::thinOld(size_t end) {
    size_t out = begin_, i = begin_;
    for (; i + 4 <= end; i += 4) {
        size_t lo = i, hi = i;
        bool gap = false;
        for (size_t k = i; k < i + 4; ++k) {
            gap = gap || !std::isfinite(v_[k]);
            if (v_[k] < v_[lo]) lo = k;
            if (v_[k] > v_[hi]) hi = k;
        }
        const size_t keep[4] = {i, i + 1, i + 2, i + 3};
        const size_t pair[2] = {std::min(lo, hi), std::max(lo, hi)};
        const size_t* from = gap ? keep : pair;
        const size_t count = gap ? 4 : (lo == hi ? 1 : 2);
        for (size_t k = 0; k < count; ++k, ++out) t_[out] = t_[from[k]], v_[out] = v_[from[k]];
    }
    for (; i < t_.size(); ++i, ++out) t_[out] = t_[i], v_[out] = v_[i];
    t_.resize(out);
    v_.resize(out);
}

size_t PlotSeries::firstAtOrAfter(double t) const {
    return size_t(std::lower_bound(t_.begin() + std::ptrdiff_t(begin_), t_.end(), t) - t_.begin()) - begin_;
}

double PlotSeries::latest() const {
    for (size_t i = t_.size(); i-- > begin_;)
        if (std::isfinite(v_[i])) return v_[i];
    return std::nan("");
}

double PlotSeries::valueAt(double t) const {
    const size_t after = size_t(std::upper_bound(t_.begin() + std::ptrdiff_t(begin_), t_.end(), t) - t_.begin());
    return after > begin_ ? v_[after - 1] : std::nan("");
}
