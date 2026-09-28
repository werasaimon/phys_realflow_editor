#pragma once
// One chart of the plots (see PlotPanel.h): lines of one unit over a window of time - or, normalised,
// any lines as shapes between 0 and 1 («форма, не величина»).
//
// The cost of a picture does not grow with the run: the chart reads only the samples inside its time
// window (the panel's: the last minute while live, or the whole run), and draws them in one of two
// ways, each a few thousand points at most:
//   - sparse (fewer samples than about 1,5 per pixel column): a 2 px line through every sample;
//   - dense: per pixel column the lowest and highest sample, joined to the column before - a ribbon
//     one polygon wide, which looks exactly like the 2 px line where the data is smooth and like a
//     band where it jitters, and is filled once instead of stroked thousands of times over itself.
//
// The anatomy follows the dataviz rules: 2 px lines, a recessive grid, text in text colours (never
// the series colour), a legend for two lines or more, a direct label at the end of each line for up
// to four (on a large chart), one y-axis only, from zero for a quantity of one sign. A solid vertical line marks the Laboratory's frame;
// the mouse draws a dashed one with a dot on every line.
//
// What the reader can do on the chart itself - all optional, the charts come by themselves:
//   - the title ends in a quiet ▾: a click opens the list of quantities for this chart (titleClicked);
//   - a legend entry: a click hides or shows its line, a double click shows it alone;
//   - a right click: the panel's menu for this chart (menuRequested);
//   - a click on the plot: that moment in the Laboratory (clicked).
#include "PlotSeries.h"

#include <QColor>
#include <QPoint>
#include <QWidget>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

// One line of a chart.
struct PlotTrack {
    std::string id;                        // the channel (never shown: the name is)
    QString name;                          // for the legend, the direct label, the tooltip
    QString unit;
    QColor color;
    Qt::PenStyle style = Qt::SolidLine;    // dashed / dotted past the palette's eighth slot
    const PlotSeries* series = nullptr;    // its history (the panel owns it)
    bool hidden = false;                   // switched off in the legend
};

class TrackChart : public QWidget {
    Q_OBJECT
public:
    explicit TrackChart(QWidget* parent = nullptr);

    void setTracks(std::vector<PlotTrack> tracks);
    void setWindow(double t0, double t1); // the span of time shown
    void refresh();                       // new samples: the y-range again, a new picture
    void setTitle(const QString& title, bool menu = true); // menu: the title ends in ▾ and opens a list
    void setEmptyText(const QString& text);                // what an empty chart says
    void setNormalized(bool on);          // each line scaled to 0 … 1 over the window
    void setTimeAxisVisible(bool on);     // lanes label the time once, under the lowest lane
    void setCursorTime(double t);         // the Laboratory's frame (NaN: none)
    void setHoverTime(double t);          // where the mouse is, on every chart of the panel (NaN: none)
    // Margins shared by stacked lanes, so their time axes line up (0: the chart's own).
    int neededLeftMargin() const;
    int neededRightMargin() const;
    void setSharedMargins(int left, int right);

    const std::vector<PlotTrack>& tracks() const { return tracks_; }
    QString title() const { return title_; }
    bool normalized() const { return normalized_; }
    double cursorTime() const { return cursor_; }
    double hoverTime() const { return hover_; }
    std::pair<double, double> timeRange() const { return {t0_, t1_}; }
    std::pair<double, double> valueRange() const { return {y0_, y1_}; } // what the y-axis shows
    // The value the chart draws for line k at time t (normalised when it is), NaN when there is none.
    double shownValueAt(size_t k, double t) const;
    double timeAtX(double x) const;
    double xAtTime(double t) const;
    int nearestTrack(const QPointF& pos) const; // the shown line nearest to the mouse (-1: none)
    QRectF titleRect() const;
    QRectF legendRect(size_t k) const;           // empty when the chart has no legend
    QRectF plotArea() const { return plotRect(); }

signals:
    void hovered(double t, QPoint globalPos, int track);
    void hoverLeft();
    void clicked(double t);
    void titleClicked();
    void legendClicked(int track);
    void legendDoubleClicked(int track);
    void menuRequested(QPoint localPos);

protected:
    void paintEvent(class QPaintEvent* e) override;
    void mouseMoveEvent(class QMouseEvent* e) override;
    void mousePressEvent(class QMouseEvent* e) override;
    void mouseDoubleClickEvent(class QMouseEvent* e) override;
    void contextMenuEvent(class QContextMenuEvent* e) override;
    void leaveEvent(class QEvent* e) override;

private:
    void updateRanges();
    QRectF plotRect() const;
    int layoutHeader(std::vector<QRectF>* legendBoxes) const; // title + legend rows: their height
    double shown(size_t k, double v) const;                    // a value as drawn (normalised if on)
    QPointF toScreen(const QRectF& r, double t, double v) const;
    int legendAt(const QPointF& pos) const;
    void drawHeader(QPainter& p) const;
    void drawGrid(QPainter& p, const QRectF& r) const;
    void drawTrack(QPainter& p, const QRectF& r, size_t k) const;
    void drawLine(QPainter& p, const QRectF& r, size_t k, size_t from, size_t to) const;
    void drawRibbon(QPainter& p, const QRectF& r, size_t k, size_t from, size_t to) const;
    void drawDirectLabels(QPainter& p, const QRectF& r) const;
    void drawTimeLine(QPainter& p, const QRectF& r, double t, bool hover) const;
    size_t shownTracks() const;
    bool directLabels() const { return shownTracks() >= 2 && shownTracks() <= 4 && width() >= 520 && height() >= 220; }

    std::vector<PlotTrack> tracks_;
    std::vector<std::pair<double, double>> own_; // each line's lowest and highest value in the window
    QString title_, emptyText_ = "нет данных";
    bool menu_ = true;
    bool normalized_ = false;
    bool timeAxis_ = true;
    double cursor_ = std::nan(""), hover_ = std::nan("");
    double t0_ = 0, t1_ = 1, y0_ = 0, y1_ = 1;
    std::vector<double> yTicks_;
    int sharedLeft_ = 0, sharedRight_ = 0;
    bool allZero_ = false; // every shown line is exactly 0 over the window (said in words)
};
