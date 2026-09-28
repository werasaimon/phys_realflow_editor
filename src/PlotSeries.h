#pragma once
// One channel's history for the plots (see PlotPanel.h), in bounded memory however long the run:
//   - a channel on a chart (the scene's energy, the selected object's height and speed, what the
//     reader added) keeps the whole run: its last 36 000 samples (10 minutes at 60 frames a second)
//     one by one, anything older thinned - every 4 old samples become their lowest and highest, in
//     the order they came, so the peaks stay while the count halves - never more than 60 000
//     samples in all (under 1 MB);
//   - any other channel keeps only its last minute (switched on, its line starts a minute back).
// A frame the channel was not measured in is a gap (NaN): the line breaks there.
#include <cstddef>
#include <vector>

class PlotSeries {
public:
    void add(double t, double v);
    void setKeep(bool wholeRun); // the whole run (thinned when old) or only the last minute
    bool keeps() const { return keep_; }
    void clear();

    size_t size() const { return t_.size() - begin_; }
    double time(size_t i) const { return t_[begin_ + i]; }
    double value(size_t i) const { return v_[begin_ + i]; }
    size_t firstAtOrAfter(double t) const; // size() when every sample is earlier
    double latest() const;                 // the last value there is (NaN: none)
    double valueAt(double t) const;        // the sample at or before t (NaN: none, or a gap)
    size_t bytes() const { return (t_.capacity() + v_.capacity()) * sizeof(double); }

private:
    void trim();
    void thinOld(size_t end);

    std::vector<double> t_, v_; // the live samples are [begin_, size)
    size_t begin_ = 0;
    bool keep_ = false;
};
