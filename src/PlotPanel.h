#pragma once
// The plots at the bottom of the window: what the scene does over time, with nothing to set up.
//
// Zero clicks. The first ▶ opens the plots by itself (MainWindow) and the scene's energy is drawn
// at once: «Энергия сцены — движения, высоты, полная (Дж)» - its kinetic and potential parts and
// their sum, the mechanical energy the SDK measures. Selecting a body - what a person does anyway to
// look at it - adds its own charts, «Высота, м» and «Скорость, м/с», under «Объект: Куб 1»;
// selecting nothing takes them away.
//
// Everything else is optional and quiet, on the charts themselves (TrackChart):
//   - the ▾ after a chart's title opens the list of quantities for that chart: a click on a row shows
//     that one instead, its ＋ adds it as another line;
//   - a click on a legend entry hides or shows its line, a double click shows it alone;
//   - a right click: add or remove a line, remove the chart, the layout, the CSV, a copy of the
//     picture, that moment in the Laboratory;
//   - the «+» after the last chart adds an empty chart with its list open;
//   - «Ещё величины…» under the charts: every quantity at once, with checkboxes.
// Three layouts: «Наложить» (charts side by side, each with its lines of one unit together),
// «Дорожками» (the charts stacked over one shared time axis), «Нормировать» (every line on one chart,
// each scaled to 0 … 1: the shape of a quantity, not its size). Never two y-axes: a quantity of
// another unit gets a chart of its own.
//
// Bounded cost, however long the run: each channel's history is a PlotSeries (the whole run, thinned
// when old, for the lines on the charts; the last minute for the rest); the charts show the last
// minute once the run is longer (the whole run on request) and draw a few thousand points at most;
// they repaint at most 25 times a second.
#include "ChannelList.h"
#include "PlotChannels.h"
#include "PlotSeries.h"
#include "TrackChart.h"

#include "core/Probe.h"
#include "scene/Channels.h"

#include <QWidget>

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class QButtonGroup;
class QLabel;
class QMenu;
class QScrollArea;
class QTimer;
class QToolButton;

class PlotPanel : public QWidget {
    Q_OBJECT
public:
    enum class Arrangement { Overlay, Lanes, Normalized };

    // One chart as the reader sees it.
    struct Chart {
        QString title;                 // fixed words («Энергия сцены — …»); empty: named after its lines
        std::vector<std::string> ids;  // its lines
        std::set<std::string> hidden;  // the lines switched off in the legend
        bool object = false;           // one of the selected object's charts: they go with the selection
    };

    explicit PlotPanel(QWidget* parent = nullptr);

    // --- Recording ---
    // A new scene: the history goes; the scene's charts as the reader left them come back (or the
    // default ones: the scene's energy).
    void setScene(const std::string& name);
    // One frame: every channel recorded, the charts repainted soon.
    void append(double t, const std::vector<std::pair<std::string, float>>& plots, const std::vector<rf::Probe::Channel>& probe,
                const std::vector<rf::Measurement>& measurements);
    void clear();
    bool empty() const { return frames_ == 0; }
    void refreshCharts(); // the charts take their new samples now (their timer does it 25 times a second)
    void setKeepEverything(bool on); // every channel keeps the whole run (the automation's --csv)
    size_t bytes() const;            // what the history holds

    // --- Files ---
    bool writeCsv(const QString& path) const;        // every channel, one column each
    bool writeVisibleCsv(const QString& path) const; // the lines on the charts: «t, с», then «name, unit»

    // The selected object («Куб 1»; empty: none): its height and speed charts, in a section of their own.
    void showObject(const QString& label);

    // --- What the reader may choose (never needed) ---
    QWidget* moreButton() const;       // «Ещё величины…»
    QWidget* arrangementSwitch() const { return switch_; }
    void setArrangement(Arrangement a);
    Arrangement arrangement() const { return arrangement_; }
    void setWholeRun(bool on);         // the whole run on the charts instead of the last minute
    void showChannel(const std::string& id);              // a line on the chart of its unit, or a new chart
    void setChannelShown(const std::string& id, bool on); // the checklist's checkbox
    bool showsChannel(const std::string& id) const;
    void applyPreset(const QString& name);                // «Энергия», «Движок», «Сбросить»
    void addChart();                                      // the «+»: an empty chart, its list open
    void openChartList(int chart);                        // the ▾ after a chart's title
    void switchChart(int chart, const std::string& id);   // the list: this quantity instead
    void setOnChart(int chart, const std::string& id, bool on); // the list's ＋ / ✓
    void toggleLine(int chart, const std::string& id);    // a click on a legend entry (-1: every chart)
    void isolateLine(int chart, const std::string& id);   // a double click on it
    void removeChart(int chart);
    QMenu* contextMenu(TrackChart* view, const QPoint& at); // the right click's menu, built, not shown

    // The Laboratory's frame as a line through every chart (NaN / clearCursor: none).
    void setCursorTime(double t);
    void clearCursor();

    // --- For the self-test ---
    const std::vector<Chart>& charts() const { return charts_; }
    const std::vector<TrackChart*>& views() const { return views_; }
    int chartOf(const TrackChart* view) const; // -1: the normalised chart of everything
    QString objectTitle() const;               // the object section's header ("" none)
    ChannelList* checklist() const { return checklist_; }
    ChannelList* chartList() const { return chartList_; }
    QLabel* placeholder() const { return placeholder_; }
    const PlotSeries* series(const std::string& id) const;
    PlotCatalog& catalog() { return catalog_; }
    QString tooltipText(const TrackChart* view, double t, int track) const;

signals:
    void timeClicked(double t);  // a click on a chart: the Laboratory's timeline goes there
    void saveCsvRequested();     // the right click's «Сохранить CSV…»
    void roomWanted(int height); // the lanes need this much height to be read

private:
    void buildBar();
    void record(double t, const std::string& id, double v);
    void addEnergyParts(double t, const std::vector<rf::Measurement>& measurements);
    bool kept(const std::string& id) const;
    void updateKeep();
    void chartsChanged(); // the reader changed the charts: the history kept, the views and lists anew
    void forgetColours();
    std::vector<ChannelList::Row> rowsFor(int chart);
    void refreshLists();
    QString titleOf(const Chart& c);
    QString unitOf(const Chart& c);
    PlotTrack trackOf(const std::string& id, bool hidden);
    void rebuildCharts();
    QWidget* section(const QString& header, const std::vector<int>& charts, bool plus);
    TrackChart* normalizedView();
    TrackChart* makeView(int chart);
    TrackChart* viewOf(int chart) const;
    std::pair<double, double> timeWindow() const;
    void alignLanes();
    void onHover(TrackChart* view, double t, QPoint globalPos, int track);
    void onHoverLeft();
    void showHint(); // before the first frame: one quiet line instead of the charts

    // The bar and the lists.
    QToolButton* more_ = nullptr;
    QWidget* switch_ = nullptr;
    QButtonGroup* switchGroup_ = nullptr;
    ChannelList* checklist_ = nullptr;
    ChannelList* chartList_ = nullptr;
    int listChart_ = -1; // the chart whose list is open
    // The charts on screen.
    QScrollArea* scroll_ = nullptr;
    QLabel* placeholder_ = nullptr;
    QTimer* repaint_ = nullptr;
    std::vector<TrackChart*> views_;
    std::vector<int> viewChart_; // the chart each view shows (-1: the normalised one)
    // What is drawn.
    std::vector<Chart> charts_;
    bool touched_ = false; // the reader changed the charts of this scene
    std::map<std::string, std::vector<Chart>> chartsByScene_;
    std::string scene_;
    QString object_;
    Arrangement arrangement_ = Arrangement::Overlay;
    bool wholeRun_ = false;
    double cursor_ = std::nan("");
    // What is recorded.
    PlotCatalog catalog_;
    std::map<std::string, PlotSeries> series_;
    std::set<std::string> reported_;      // the channels of the frame being recorded
    std::vector<std::string> plotNames_;  // the solvers' own readings (first in the CSV)
    bool fresh_ = false;                  // a channel appeared in this frame
    bool keepAll_ = false;
    double start_ = 0, now_ = 0;
    long frames_ = 0;
};
