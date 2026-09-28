// One chart of the plots: lines of one unit over a window of time, drawn by hand (see TrackChart.h).
#include "TrackChart.h"

#include "PlotChannels.h"

#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>

namespace {

// The dark editor theme, the same tokens as the rest of the panel.
const QColor kSurface(28, 30, 35);     // #1c1e23: the chart surface the palette was validated on
const QColor kGrid(46, 50, 58);        // recessive: the data is the figure, the grid the ground
const QColor kAxisText(150, 157, 170); // secondary text
const QColor kInk(225, 228, 235);      // primary text
const QColor kMuted(110, 117, 130);

constexpr double kHeaderRow = 18.0;   // one row of title / legend
constexpr double kBottomAxis = 22.0;
constexpr double kLabelWidth = 118.0; // a direct label at most this wide (elided)
constexpr double kLegendText = 240.0; // a legend entry's words at most this wide (elided)
constexpr double kDense = 1.5;        // samples per pixel column from which a line is drawn as a ribbon

// Round tick values across [lo, hi]: steps of 1, 2 or 5 × 10^k, at most about maxTicks of them; the
// step comes back in *stepOut (the labels' decimals follow it).
std::vector<double> niceTicks(double lo, double hi, int maxTicks, double* stepOut = nullptr) {
    if (stepOut) *stepOut = 0;
    if (!(hi > lo) || !std::isfinite(hi - lo)) return {lo};
    const double raw = (hi - lo) / std::max(1, maxTicks - 1);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double r = raw / mag;
    const double step = (r <= 1 ? 1 : r <= 2 ? 2 : r <= 5 ? 5 : 10) * mag;
    if (stepOut) *stepOut = step;
    std::vector<double> ticks;
    for (double v = std::ceil(lo / step) * step; v <= hi + step * 1e-9 && ticks.size() < 50; v += step)
        ticks.push_back(std::fabs(v) < step * 1e-9 ? 0.0 : v);
    return ticks;
}

// A tick's number the Russian way (a decimal comma), with as many decimals as the tick step needs,
// the same for every tick of an axis; zero is «0»; only the very large, or a step finer than a
// millionth, as «1,5·10⁻⁶» (numberText).
QString formatNumber(double v, double step) {
    const double a = std::fabs(v);
    if (a <= step * 1e-6) return "0";
    if (a >= 1e5 || (step > 0 && step < 1e-6)) return numberText(v, 3);
    const int decimals = step > 0 ? std::clamp(int(-std::floor(std::log10(step))), 0, 6) : 3;
    QString s = QString::number(v, 'f', decimals);
    if (s.startsWith('-') && s.toDouble() == 0.0) s.remove(0, 1);
    return s.replace('.', ',');
}

// What a direct label calls a line: the name without its part («Твёрдые тела: ») and without its
// formula («, Σ m v²/2») - the legend above keeps the whole name.
QString shortName(const QString& name) {
    const int colon = name.indexOf(": ");
    QString s = colon > 0 ? name.mid(colon + 2) : name;
    for (const char* formula : {", Σ", ", ∫"})
        if (const int at = s.indexOf(QString::fromUtf8(formula)); at > 0) s.truncate(at);
    return s;
}

// The legend's words for a line; on the normalised chart units differ, so each name carries its own.
QString legendText(const PlotTrack& t, bool normalized) {
    return normalized && !t.unit.isEmpty() ? t.name + ", " + t.unit : t.name;
}

// The legend's words for all lines, less the last words every name shares: «Кинетическая энергия»,
// «Потенциальная энергия», «Полная энергия» read «Кинетическая», «Потенциальная», «Полная» - the
// title says what they have in common, and a short legend leaves the plot its height.
std::vector<QString> legendTexts(const std::vector<PlotTrack>& tracks, bool normalized) {
    std::vector<QStringList> words;
    for (const PlotTrack& t : tracks) words.push_back(legendText(t, normalized).split(' '));
    int shared = 0;
    for (bool same = words.size() >= 2; same; shared += same ? 1 : 0)
        for (const QStringList& w : words)
            same = same && w.size() > shared + 1 && w[w.size() - 1 - shared] == words[0][words[0].size() - 1 - shared];
    std::vector<QString> texts;
    for (const QStringList& w : words) texts.push_back(w.mid(0, w.size() - shared).join(' '));
    return texts;
}

QFont smallFont(const QWidget* w, qreal points, bool bold = false) {
    QFont f = w->font();
    f.setPointSizeF(points);
    f.setBold(bold);
    return f;
}

// The fill of a ribbon: solid for the first eight lines, a hatch for the dashed and dotted ones.
Qt::BrushStyle ribbonBrush(Qt::PenStyle style) {
    return style == Qt::SolidLine ? Qt::SolidPattern : style == Qt::DashLine ? Qt::Dense3Pattern : Qt::Dense5Pattern;
}

// The samples of a series inside [t0, t1], and one on each side, so that a line runs to the edges.
std::pair<size_t, size_t> inWindow(const PlotSeries& s, double t0, double t1) {
    size_t from = s.firstAtOrAfter(t0), to = s.firstAtOrAfter(t1);
    while (to < s.size() && s.time(to) <= t1) ++to;
    return {from > 0 ? from - 1 : 0, std::min(s.size(), to + 1)};
}

} // namespace

TrackChart::TrackChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(220, 120);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void TrackChart::setTracks(std::vector<PlotTrack> tracks) {
    tracks_ = std::move(tracks);
    refresh();
}

void TrackChart::setWindow(double t0, double t1) {
    t0_ = t0;
    t1_ = t1 > t0 ? t1 : t0 + 1e-3;
}

void TrackChart::refresh() {
    updateRanges();
    update();
}

void TrackChart::setTitle(const QString& title, bool menu) { title_ = title, menu_ = menu, update(); }
void TrackChart::setEmptyText(const QString& text) { emptyText_ = text, update(); }
void TrackChart::setNormalized(bool on) { normalized_ = on, refresh(); }
void TrackChart::setTimeAxisVisible(bool on) { timeAxis_ = on, update(); }
void TrackChart::setCursorTime(double t) { cursor_ = t, update(); }
void TrackChart::setHoverTime(double t) { hover_ = t, update(); }
void TrackChart::setSharedMargins(int left, int right) { sharedLeft_ = left, sharedRight_ = right, update(); }

size_t TrackChart::shownTracks() const {
    return size_t(std::count_if(tracks_.begin(), tracks_.end(), [](const PlotTrack& t) { return !t.hidden; }));
}

// The y-axis for the window (the time axis is the panel's):
//   1. each line's lowest and highest value inside the window (normalisation divides by them);
//   2. 0 … 1 when normalised, else the shown lines' lowest to highest, padded 6 %;
//   3. round ticks over it.
void TrackChart::updateRanges() {
    own_.assign(tracks_.size(), {std::nan(""), std::nan("")});
    double lo = INFINITY, hi = -INFINITY;
    allZero_ = false;
    for (size_t k = 0; k < tracks_.size(); ++k) {
        if (!tracks_[k].series) continue;
        const PlotSeries& s = *tracks_[k].series;
        const auto [from, to] = inWindow(s, t0_, t1_);
        double a = INFINITY, b = -INFINITY;
        for (size_t i = from; i < to; ++i)
            if (const double v = s.value(i); std::isfinite(v)) a = std::min(a, v), b = std::max(b, v);
        if (!std::isfinite(a)) continue;
        own_[k] = {a, b};
        if (!tracks_[k].hidden) lo = std::min(lo, a), hi = std::max(hi, b);
    }
    allZero_ = lo == 0 && hi == 0; // nothing moves, nothing falls: a flat zero, said in words below
    if (normalized_ || !std::isfinite(lo)) lo = 0.0, hi = 1.0;
    // A quantity of one sign (a height, a speed, an energy) is measured from zero: a small wobble
    // then looks small. The axis never spans less than a thousandth of the unit: rounding noise
    // around a resting value draws as the flat line it is.
    lo = std::min(lo, 0.0);
    hi = std::max(hi, 0.0);
    const double least = std::max(1e-3, 1e-6 * std::max(std::fabs(lo), std::fabs(hi)));
    if (hi - lo < least) {
        if (lo == 0) hi = least;
        else lo = -least;
    }
    const double pad = (hi - lo) * 0.06;
    y0_ = normalized_ ? -0.04 : lo - (lo == 0 ? pad / 3 : pad);
    y1_ = normalized_ ? 1.04 : hi + (hi == 0 ? pad / 3 : pad);
    yTicks_ = niceTicks(normalized_ ? 0.0 : y0_, normalized_ ? 1.0 : y1_, 5);
}

double TrackChart::shown(size_t k, double v) const {
    if (!normalized_ || !std::isfinite(v)) return v;
    const double span = own_[k].second - own_[k].first;
    return span > 0 ? (v - own_[k].first) / span : 0.5; // a flat line sits in the middle
}

double TrackChart::shownValueAt(size_t k, double t) const {
    if (k >= tracks_.size() || !tracks_[k].series) return std::nan("");
    return shown(k, tracks_[k].series->valueAt(t));
}

// The y-tick labels decide the left margin; the direct labels the right one.
int TrackChart::neededLeftMargin() const {
    const QFontMetricsF fm(smallFont(this, 8));
    const double step = yTicks_.size() > 1 ? yTicks_[1] - yTicks_[0] : 0.0;
    double widest = 0;
    for (double v : yTicks_) widest = std::max(widest, fm.horizontalAdvance(formatNumber(v, step)));
    return std::max(36, int(std::ceil(widest)) + 12); // room for «t, с» under the labels too
}

int TrackChart::neededRightMargin() const {
    if (!directLabels()) return 14;
    const QFontMetricsF fm(smallFont(this, 8));
    double widest = 0;
    for (const PlotTrack& t : tracks_)
        if (!t.hidden) widest = std::max(widest, std::min(kLabelWidth, fm.horizontalAdvance(shortName(t.name))));
    return int(std::ceil(widest)) + 22;
}

QRectF TrackChart::plotRect() const {
    const double left = sharedLeft_ > 0 ? sharedLeft_ : neededLeftMargin();
    const double right = sharedRight_ > 0 ? sharedRight_ : neededRightMargin();
    const double top = layoutHeader(nullptr) + 4.0;
    const double bottom = timeAxis_ ? kBottomAxis : 8.0;
    return QRectF(left, top, std::max(10.0, width() - left - right), std::max(10.0, height() - top - bottom));
}

double TrackChart::timeAtX(double x) const {
    const QRectF r = plotRect();
    return t0_ + (std::clamp(x, r.left(), r.right()) - r.left()) / r.width() * (t1_ - t0_);
}

double TrackChart::xAtTime(double t) const { return toScreen(plotRect(), t, y0_).x(); }

QPointF TrackChart::toScreen(const QRectF& r, double t, double v) const {
    return {r.left() + (t - t0_) / (t1_ - t0_) * r.width(), r.bottom() - (v - y0_) / (y1_ - y0_) * r.height()};
}

// The shown line whose value at the mouse's moment is nearest to the mouse, on the screen.
int TrackChart::nearestTrack(const QPointF& pos) const {
    const QRectF r = plotRect();
    const double t = timeAtX(pos.x());
    int best = -1;
    double gap = INFINITY;
    for (size_t k = 0; k < tracks_.size(); ++k) {
        const double v = tracks_[k].hidden ? std::nan("") : shownValueAt(k, t);
        if (std::isfinite(v) && std::fabs(toScreen(r, t, v).y() - pos.y()) < gap) gap = std::fabs(toScreen(r, t, v).y() - pos.y()), best = int(k);
    }
    return best;
}

// --- The header: title and legend -----------------------------------------------------------------

QRectF TrackChart::titleRect() const {
    const QFontMetricsF titleFm(smallFont(this, 9, true)), fm(smallFont(this, 8));
    const double w = std::min(width() - 16.0, titleFm.horizontalAdvance(title_)) + (menu_ ? fm.horizontalAdvance("  ▾") : 0.0);
    return QRectF(4, 1, w + 10, kHeaderRow + 2);
}

// The title, then - for two lines or more - the legend: one entry per line (a 16 px stroke of the
// line's own colour and its name), after the title and wrapping onto more rows when the chart is
// narrow. Returns the header's height; fills the entries' boxes when asked.
int TrackChart::layoutHeader(std::vector<QRectF>* boxes) const {
    const QFontMetricsF fm(smallFont(this, 8));
    double x = titleRect().right() + 14, y = 2;
    if (tracks_.size() < 2) return int(y + kHeaderRow + 2); // one line: the title names it
    for (const QString& text : legendTexts(tracks_, normalized_)) {
        const double w = 16 + 5 + std::min(kLegendText, fm.horizontalAdvance(text));
        if (x + w > width() - 8 && x > 8) x = 8, y += kHeaderRow; // wrap onto the next row
        if (boxes) boxes->push_back(QRectF(x, y, w, kHeaderRow));
        x += w + 14;
    }
    return int(y + kHeaderRow + 2);
}

QRectF TrackChart::legendRect(size_t k) const {
    std::vector<QRectF> boxes;
    layoutHeader(&boxes);
    return k < boxes.size() ? boxes[k] : QRectF();
}

int TrackChart::legendAt(const QPointF& pos) const {
    std::vector<QRectF> boxes;
    layoutHeader(&boxes);
    for (size_t k = 0; k < boxes.size(); ++k)
        if (boxes[k].adjusted(-3, 0, 3, 0).contains(pos)) return int(k);
    return -1;
}

// The title in bold, a quiet ▾ after it (a click opens the list); the legend entries, a hidden
// line's entry dimmed.
void TrackChart::drawHeader(QPainter& p) const {
    p.setFont(smallFont(this, 9, true));
    p.setPen(kInk);
    const QFontMetricsF titleFm(p.font());
    const QString title = titleFm.elidedText(title_, Qt::ElideRight, width() - 40);
    p.drawText(QRectF(8, 2, width() - 16, kHeaderRow), Qt::AlignLeft | Qt::AlignVCenter, title);
    p.setFont(smallFont(this, 8));
    if (menu_) {
        p.setPen(kMuted);
        p.drawText(QRectF(8 + titleFm.horizontalAdvance(title), 2, 30, kHeaderRow), Qt::AlignLeft | Qt::AlignVCenter, "  ▾");
    }
    std::vector<QRectF> boxes;
    layoutHeader(&boxes);
    const std::vector<QString> texts = legendTexts(tracks_, normalized_);
    const QFontMetricsF fm(p.font());
    for (size_t k = 0; k < boxes.size(); ++k) {
        const QRectF& b = boxes[k];
        const double mid = b.center().y();
        QColor stroke = tracks_[k].color;
        if (tracks_[k].hidden) stroke.setAlpha(70);
        p.setPen(QPen(stroke, 2.0, tracks_[k].style, Qt::FlatCap));
        p.drawLine(QPointF(b.left(), mid), QPointF(b.left() + 16, mid));
        p.setPen(tracks_[k].hidden ? kMuted : kInk); // the words in text colour: the stroke carries the identity
        const QRectF text = b.adjusted(21, 0, 0, 0);
        p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(texts[k], Qt::ElideRight, text.width() + 1));
    }
}

// --- The plot: grid, lines, labels ----------------------------------------------------------------

// Horizontal grid lines at round values, their numbers in the left margin; round times under the
// plot when this chart shows the time axis, «t, с» in the corner.
void TrackChart::drawGrid(QPainter& p, const QRectF& r) const {
    p.setFont(smallFont(this, 8));
    const double step = yTicks_.size() > 1 ? yTicks_[1] - yTicks_[0] : 0.0;
    for (double v : yTicks_) {
        const double y = std::round(toScreen(r, t0_, v).y()) + 0.5;
        p.setPen(QPen(kGrid, 1.0));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(kAxisText);
        p.drawText(QRectF(0, y - 8, r.left() - 7, 16), Qt::AlignRight | Qt::AlignVCenter, formatNumber(v, step));
    }
    if (!timeAxis_) return;
    double tStep = 0;
    const std::vector<double> ticks = niceTicks(t0_, t1_, std::max(2, int(r.width() / 70)), &tStep);
    for (double t : ticks) {
        const double x = std::round(toScreen(r, t, y0_).x()) + 0.5;
        p.setPen(QPen(kGrid, 1.0));
        p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 4));
        p.setPen(kAxisText);
        p.drawText(QRectF(x - 40, r.bottom() + 5, 80, 14), Qt::AlignHCenter | Qt::AlignTop, formatNumber(t, tStep));
    }
    p.setPen(kMuted);
    p.drawText(QRectF(0, r.bottom() + 5, r.left() - 12, 14), Qt::AlignRight | Qt::AlignTop, "t, с");
}

// One line: the samples inside the window, as a 2 px line when they are sparse and as a ribbon when
// they are dense (see the file head of TrackChart.h).
void TrackChart::drawTrack(QPainter& p, const QRectF& r, size_t k) const {
    if (!tracks_[k].series || tracks_[k].hidden) return;
    const auto [from, to] = inWindow(*tracks_[k].series, t0_, t1_);
    if (to <= from) return;
    if (double(to - from) > kDense * r.width()) drawRibbon(p, r, k, from, to);
    else drawLine(p, r, k, from, to);
}

// Sparse samples: a 2 px line through each of them; a gap (NaN) breaks it, a lone sample is a dot.
void TrackChart::drawLine(QPainter& p, const QRectF& r, size_t k, size_t from, size_t to) const {
    const PlotTrack& track = tracks_[k];
    const PlotSeries& s = *track.series;
    const QPen pen(track.color, 2.0, track.style, Qt::RoundCap, Qt::RoundJoin);
    QPolygonF piece;
    auto flush = [&] {
        if (piece.size() == 1) {
            p.setPen(Qt::NoPen);
            p.setBrush(track.color);
            p.drawEllipse(piece.front(), 2.0, 2.0);
        } else if (piece.size() > 1) {
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(piece);
        }
        piece.clear();
    };
    for (size_t i = from; i < to; ++i) {
        const double v = s.value(i);
        if (std::isfinite(v)) piece << toScreen(r, s.time(i), shown(k, v));
        else flush();
    }
    flush();
}

// Dense samples, one pixel column at a time:
//   1. each column spans its lowest and highest sample, stretched to the last sample of the column
//      before (so a steep slope never breaks the ribbon) - empty columns between two samples get the
//      straight line across them;
//   2. each span at least 2 px thick, so a smooth stretch looks like the line;
//   3. every unbroken run of columns is one polygon - the top edge there, the bottom edge back -
//      filled once; a gap (NaN) starts a new run.
void TrackChart::drawRibbon(QPainter& p, const QRectF& r, size_t k, size_t from, size_t to) const {
    const PlotSeries& s = *tracks_[k].series;
    const int x0 = int(std::floor(r.left())) - 2, columns = int(std::ceil(r.width())) + 5;
    std::vector<double> top(size_t(columns), INFINITY), bottom(size_t(columns), -INFINITY);
    std::vector<char> startsRun(size_t(columns), 0);
    int lastColumn = -1;
    double lastY = std::nan("");
    for (size_t i = from; i < to; ++i) {
        const double v = s.value(i);
        if (!std::isfinite(v)) {
            lastY = std::nan("");
            continue;
        }
        const QPointF pt = toScreen(r, s.time(i), shown(k, v));
        const int c = std::clamp(int(std::floor(pt.x())) - x0, 0, columns - 1);
        if (!std::isfinite(lastY)) startsRun[size_t(c)] = 1;
        for (int e = lastColumn + 1; std::isfinite(lastY) && e < c; ++e) { // the empty columns between
            const double y = lastY + (pt.y() - lastY) * double(e - lastColumn) / double(c - lastColumn);
            top[size_t(e)] = std::min(top[size_t(e)], y), bottom[size_t(e)] = std::max(bottom[size_t(e)], y);
        }
        const double a = std::isfinite(lastY) ? std::min(pt.y(), lastY) : pt.y();
        const double b = std::isfinite(lastY) ? std::max(pt.y(), lastY) : pt.y();
        top[size_t(c)] = std::min(top[size_t(c)], a), bottom[size_t(c)] = std::max(bottom[size_t(c)], b);
        lastColumn = c, lastY = pt.y();
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(tracks_[k].color, ribbonBrush(tracks_[k].style)));
    QPolygonF upper, lower;
    auto flush = [&] {
        for (int i = int(lower.size()) - 1; i >= 0; --i) upper << lower[i];
        if (upper.size() > 2) p.drawPolygon(upper);
        upper.clear(), lower.clear();
    };
    for (int c = 0; c < columns; ++c) {
        if (!(top[size_t(c)] <= bottom[size_t(c)]) || startsRun[size_t(c)]) flush();
        if (!(top[size_t(c)] <= bottom[size_t(c)])) continue;
        const double mid = (top[size_t(c)] + bottom[size_t(c)]) / 2, x = x0 + c + 0.5;
        upper << QPointF(x, std::min(top[size_t(c)], mid - 1.0));
        lower << QPointF(x, std::max(bottom[size_t(c)], mid + 1.0));
    }
    flush();
}

// Each line's name at its right end (two to four lines, on a wide chart): a dot where the line stops
// and the short name in the margin beside it, the labels nudged apart so that none covers another.
void TrackChart::drawDirectLabels(QPainter& p, const QRectF& r) const {
    if (!directLabels()) return;
    struct Label {
        size_t k;
        QPointF end; // where the line stops
        double y;    // where its label goes
    };
    std::vector<Label> labels;
    for (size_t k = 0; k < tracks_.size(); ++k) {
        if (tracks_[k].hidden || !tracks_[k].series) continue;
        const PlotSeries& s = *tracks_[k].series;
        const auto [from, to] = inWindow(s, t0_, t1_);
        for (size_t i = to; i-- > from;) // its last value in the window
            if (s.time(i) <= t1_ && std::isfinite(s.value(i))) {
                const QPointF end = toScreen(r, s.time(i), shown(k, s.value(i)));
                labels.push_back({k, end, end.y()});
                break;
            }
    }
    std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.y < b.y; });
    for (size_t i = 1; i < labels.size(); ++i) labels[i].y = std::max(labels[i].y, labels[i - 1].y + 13.0);
    const double overflow = labels.empty() ? 0.0 : labels.back().y - (height() - 8.0);
    for (Label& l : labels) l.y -= std::max(0.0, overflow);

    p.setFont(smallFont(this, 8));
    const QFontMetricsF fm(p.font());
    for (const Label& l : labels) {
        p.setPen(QPen(kSurface, 2.0)); // a ring of the surface keeps the dot apart from lines under it
        p.setBrush(tracks_[l.k].color);
        p.drawEllipse(l.end, 3.5, 3.5);
        p.setPen(kInk);
        p.drawText(QRectF(r.right() + 10, l.y - 8, kLabelWidth + 4, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(shortName(tracks_[l.k].name), Qt::ElideRight, kLabelWidth));
    }
}

// A vertical line through the plot at time t: the Laboratory's frame (solid, its time on top) or
// the mouse (dashed, a dot on every line - the panel's tooltip gives the value).
void TrackChart::drawTimeLine(QPainter& p, const QRectF& r, double t, bool hover) const {
    if (!std::isfinite(t) || t < t0_ || t > t1_) return;
    const double x = toScreen(r, t, y0_).x();
    QColor ink = kInk;
    ink.setAlpha(hover ? 130 : 180);
    p.setPen(QPen(ink, hover ? 1.0 : 1.5, hover ? Qt::DashLine : Qt::SolidLine));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    if (hover) {
        for (size_t k = 0; k < tracks_.size(); ++k)
            if (const double v = shownValueAt(k, t); !tracks_[k].hidden && std::isfinite(v)) {
                p.setPen(QPen(kSurface, 2.0));
                p.setBrush(tracks_[k].color);
                p.drawEllipse(toScreen(r, t, v), 4.0, 4.0);
            }
        return;
    }
    p.setFont(smallFont(this, 8));
    const QFontMetricsF fm(p.font());
    const QString label = QString("t = %1 с").arg(formatNumber(t, 0.01));
    const double w = fm.horizontalAdvance(label) + 10;
    const double left = x + 4 + w <= r.right() ? x + 4 : x - 4 - w; // flip left at the right edge
    const QRectF box(left, r.top() + 2, w, 16);
    QColor back = kSurface, edge = kInk;
    back.setAlpha(225);
    edge.setAlpha(90);
    p.setPen(QPen(edge, 1.0));
    p.setBrush(back);
    p.drawRoundedRect(box, 3, 3);
    p.setPen(kInk);
    p.drawText(box, Qt::AlignCenter, label);
}

void TrackChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kSurface);
    p.setRenderHint(QPainter::Antialiasing);
    drawHeader(p);
    const QRectF r = plotRect();
    const bool any = std::any_of(own_.begin(), own_.end(), [](const auto& o) { return std::isfinite(o.first); });
    if (!any) {
        p.setFont(smallFont(this, 9));
        p.setPen(kMuted);
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, emptyText_);
        return;
    }
    drawGrid(p, r);
    p.save();
    p.setClipRect(r.adjusted(-3, -3, 3, 3));
    for (size_t k = 0; k < tracks_.size(); ++k) drawTrack(p, r, k);
    p.restore();
    drawDirectLabels(p, r);
    if (allZero_) {
        p.setFont(smallFont(this, 9));
        p.setPen(kAxisText);
        p.drawText(r.adjusted(0, 6, 0, 0), Qt::AlignHCenter | Qt::AlignTop, "всё это время — ровно 0");
    }
    drawTimeLine(p, r, cursor_, false);
    drawTimeLine(p, r, hover_, true);
}

// --- The mouse ------------------------------------------------------------------------------------

// Over the title or a legend entry: a hand (they answer a click). Over the plot: the moment and the
// line nearest to the mouse, for the panel's tooltip.
void TrackChart::mouseMoveEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    if ((menu_ && titleRect().contains(pos)) || legendAt(pos) >= 0) {
        setCursor(Qt::PointingHandCursor);
        emit hoverLeft();
        return;
    }
    if (plotRect().adjusted(-4, 0, 4, 0).contains(pos)) {
        setCursor(Qt::CrossCursor);
        emit hovered(timeAtX(pos.x()), e->globalPosition().toPoint(), nearestTrack(pos));
    } else {
        unsetCursor();
        emit hoverLeft();
    }
}

void TrackChart::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPointF pos = e->position();
    if (menu_ && titleRect().contains(pos)) emit titleClicked();
    else if (const int k = legendAt(pos); k >= 0) emit legendClicked(k);
    else if (plotRect().adjusted(-4, 0, 4, 0).contains(pos)) emit clicked(timeAtX(pos.x()));
}

void TrackChart::mouseDoubleClickEvent(QMouseEvent* e) {
    if (const int k = legendAt(e->position()); e->button() == Qt::LeftButton && k >= 0) emit legendDoubleClicked(k);
}

void TrackChart::contextMenuEvent(QContextMenuEvent* e) { emit menuRequested(e->pos()); }

void TrackChart::leaveEvent(QEvent*) { emit hoverLeft(); }
