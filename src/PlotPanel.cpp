// The plots at the bottom of the window (see PlotPanel.h): the recording, the charts that come by
// themselves, what the reader may change on them, the hover, the cursor and the CSV.
#include "PlotPanel.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr double kLiveSeconds = 60.0; // a longer run shows its last minute, sliding

// THE one place where the first chart is chosen: the scene's energy, its three lines in this order -
// of motion, of height, and their sum (the mechanical energy the SDK measures while the plots or
// the Laboratory are open).
const std::vector<std::string>& defaultChannels() {
    static const std::vector<std::string> channels = {"scene/kinetic energy", "scene/potential energy", "scene/mechanical energy"};
    return channels;
}

std::vector<PlotPanel::Chart> defaultCharts() {
    return {{"Энергия сцены — движения, высоты, полная (Дж)", defaultChannels(), {}, false}};
}

// The selected object's two charts: how high it is and how fast it moves.
std::vector<PlotPanel::Chart> objectCharts(const QString& label) {
    const std::string l = label.toStdString();
    return {{"Высота, м", {l + "/height"}, {}, true}, {"Скорость, м/с", {l + "/speed"}, {}, true}};
}

QString formatValue(double v) { return numberText(v, 4); }

void eraseId(std::vector<std::string>& ids, const std::string& id) { ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end()); }

bool contains(const std::vector<std::string>& ids, const std::string& id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

} // namespace

PlotPanel::PlotPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("plotPanel");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // Before the first frame: one quiet line, nothing to press.
    placeholder_ = new QLabel("Графики появятся после ▶");
    placeholder_->setObjectName("plotPlaceholder");
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setStyleSheet("color: #8a93a3;");
    col->addWidget(placeholder_, 1);

    // The charts scroll: many of them never squeeze below a readable size.
    scroll_ = new QScrollArea;
    scroll_->setObjectName("plotScroll");
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    col->addWidget(scroll_, 1);

    checklist_ = new ChannelList(ChannelList::Mode::Checklist, this);
    connect(checklist_, &ChannelList::toggled, this, [this](std::string id, bool on) { setChannelShown(id, on); });
    connect(checklist_, &ChannelList::presetChosen, this, &PlotPanel::applyPreset);
    chartList_ = new ChannelList(ChannelList::Mode::Chart, this);
    connect(chartList_, &ChannelList::chosen, this, [this](std::string id) { switchChart(listChart_, id); });
    connect(chartList_, &ChannelList::toggled, this, [this](std::string id, bool on) { setOnChart(listChart_, id, on); });
    buildBar();

    // The frames come 60 times a second; the charts repaint at most 25 times a second.
    repaint_ = new QTimer(this);
    repaint_->setSingleShot(true);
    repaint_->setInterval(40);
    connect(repaint_, &QTimer::timeout, this, &PlotPanel::refreshCharts);
    for (const std::string& id : defaultChannels()) catalog_.pin(id); // blue, orange, green: always the same
    charts_ = defaultCharts();
    rebuildCharts();
}

// Under the charts, quiet: «Ещё величины…» (every quantity, with checkboxes) and the layout switch -
// «Наложить | Дорожками | Нормировать», a segmented control, always one pressed.
void PlotPanel::buildBar() {
    more_ = new QToolButton;
    more_->setObjectName("plotMore");
    more_->setText("Ещё величины…");
    more_->setToolTip("Все величины, которые считает движок, с галочками — для тех, кому нужно всё");
    connect(more_, &QToolButton::clicked, this, [this] {
        checklist_->setRows(rowsFor(-1));
        checklist_->popupAt(more_, more_->rect());
    });
    switch_ = new QWidget;
    switch_->setObjectName("plotLayoutSwitch");
    auto* row = new QHBoxLayout(switch_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    switchGroup_ = new QButtonGroup(this);
    const std::pair<const char*, const char*> choices[] = {{"overlay", "Наложить"}, {"lanes", "Дорожками"}, {"normalized", "Нормировать"}};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QToolButton;
        b->setObjectName(QString("plotLayout_") + choices[i].first);
        b->setText(choices[i].second);
        b->setCheckable(true);
        b->setChecked(i == int(arrangement_));
        switchGroup_->addButton(b, i);
        row->addWidget(b);
    }
    switchGroup_->button(0)->setToolTip("Графики рядом; на каждом — линии одной единицы");
    switchGroup_->button(1)->setToolTip("Графики друг под другом, с общей осью времени");
    switchGroup_->button(2)->setToolTip("Все линии на одном графике, каждая от 0 до 1: форма, не величина");
    const QString quiet = "QToolButton { color: #aab1bd; border: 1px solid #353a43; padding: 2px 8px; background: transparent; }"
                          "QToolButton:hover { background: #2f3540; }"
                          "QToolButton:checked { color: white; background: #2a3a52; border-color: #3d5a80; }";
    switch_->setStyleSheet(quiet + "QToolButton#plotLayout_overlay { border-top-left-radius: 5px; border-bottom-left-radius: 5px; }"
                                   "QToolButton#plotLayout_normalized { border-top-right-radius: 5px; border-bottom-right-radius: 5px; }");
    more_->setStyleSheet("QToolButton { color: #aab1bd; border: none; padding: 2px 6px; } QToolButton:hover { color: white; }");
    connect(switchGroup_, &QButtonGroup::idClicked, this, [this](int id) { setArrangement(Arrangement(id)); });
}

QWidget* PlotPanel::moreButton() const { return more_; }

void PlotPanel::setArrangement(Arrangement a) {
    arrangement_ = a;
    if (QAbstractButton* b = switchGroup_->button(int(a))) b->setChecked(true);
    rebuildCharts();
}

void PlotPanel::setWholeRun(bool on) {
    wholeRun_ = on;
    refreshCharts();
}

// --- The recording --------------------------------------------------------------------------------

void PlotPanel::setScene(const std::string& name) {
    std::vector<Chart> own;
    for (const Chart& c : charts_)
        if (!c.object) own.push_back(c);
    if (!scene_.empty() && touched_) chartsByScene_[scene_] = own;
    scene_ = name;
    const auto it = chartsByScene_.find(name);
    touched_ = it != chartsByScene_.end();
    charts_ = touched_ ? it->second : defaultCharts();
    object_.clear(); // the object's section goes with the old scene
    clear();
}

// One frame:
//   1. the SDK's name cards of this frame's measurements are learnt (an object's channels come so);
//   2. every value is recorded - the solvers' readings, the Probe's channels, the measurements - and
//      the scene's energy in its two parts (addEnergyParts); a channel the SDK measures is taken from
//      the measurements only (the Probe may still hold its value from an earlier scene);
//   3. a channel that was not in this frame gets a gap, so its line breaks instead of lying;
//   4. a new channel: the history's bounds, the charts waiting for it, the lists follow;
//   5. the charts repaint soon (at most 25 times a second).
void PlotPanel::append(double t, const std::vector<std::pair<std::string, float>>& plots, const std::vector<rf::Probe::Channel>& probe,
                       const std::vector<rf::Measurement>& measurements) {
    catalog_.learn(measurements);
    if (frames_++ == 0) start_ = t, showHint();
    now_ = t;
    reported_.clear();
    plotNames_.clear();
    for (const auto& [name, v] : plots) {
        plotNames_.push_back(name);
        record(t, name, v);
    }
    for (const rf::Probe::Channel& c : probe)
        if (!catalog_.measuredBySdk(c.name)) record(t, c.name, c.value);
    for (const rf::Measurement& m : measurements) record(t, m.info.id, m.value);
    addEnergyParts(t, measurements);
    for (auto& [id, s] : series_)
        if (!reported_.count(id)) s.add(t, std::nan(""));
    if (fresh_) {
        fresh_ = false;
        updateKeep();
        rebuildCharts(); // a chart waiting for this channel gets its line
        refreshLists();
    }
    if (!repaint_->isActive()) repaint_->start();
}

void PlotPanel::record(double t, const std::string& id, double v) {
    auto [it, added] = series_.try_emplace(id);
    if (added) fresh_ = true, it->second.setKeep(kept(id));
    it->second.add(t, v);
    reported_.insert(id);
}

// The scene's energy in the two parts the first chart shows (the SDK measures their sum): of motion
// - the bodies' translation and rotation, the particles, the gas - and of height - the bodies and
// the particles. Added exactly as the SDK adds the total (scene/Channels.cpp), so the three agree.
void PlotPanel::addEnergyParts(double t, const std::vector<rf::Measurement>& measurements) {
    static const std::set<std::string> motion = {"rigid/kinetic energy", "rigid/rotational energy", "particles/kinetic energy",
                                                 "gas/kinetic energy"};
    static const std::set<std::string> height = {"rigid/potential energy", "particles/potential energy"};
    double kinetic = 0, potential = 0;
    bool total = false;
    for (const rf::Measurement& m : measurements) {
        if (motion.count(m.info.id)) kinetic += m.value;
        if (height.count(m.info.id)) potential += m.value;
        total = total || m.info.id == "scene/mechanical energy";
    }
    if (!total) return;
    record(t, "scene/kinetic energy", kinetic);
    record(t, "scene/potential energy", potential);
}

// Which channels keep the whole run: the lines on the charts (the selected object's among them), the
// scene's energy, and everything for the automation's CSV.
bool PlotPanel::kept(const std::string& id) const {
    if (keepAll_ || contains(defaultChannels(), id)) return true;
    return std::any_of(charts_.begin(), charts_.end(), [&](const Chart& c) { return contains(c.ids, id); });
}

void PlotPanel::updateKeep() {
    for (auto& [id, s] : series_) s.setKeep(kept(id));
}

void PlotPanel::setKeepEverything(bool on) {
    keepAll_ = on;
    updateKeep();
}

size_t PlotPanel::bytes() const {
    size_t sum = 0;
    for (const auto& [id, s] : series_) sum += s.bytes() + id.capacity();
    return sum;
}

void PlotPanel::clear() {
    series_.clear();
    plotNames_.clear();
    frames_ = 0;
    cursor_ = std::nan("");
    rebuildCharts(); // the views pointed into the history that is gone
    refreshLists();
}

const PlotSeries* PlotPanel::series(const std::string& id) const {
    const auto it = series_.find(id);
    return it != series_.end() ? &it->second : nullptr;
}

// --- The selected object --------------------------------------------------------------------------

void PlotPanel::showObject(const QString& label) {
    if (label == object_) return;
    object_ = label;
    charts_.erase(std::remove_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.object; }), charts_.end());
    if (!label.isEmpty())
        for (const Chart& c : objectCharts(label)) charts_.push_back(c);
    updateKeep();
    forgetColours();
    rebuildCharts();
    QTimer::singleShot(0, this, &PlotPanel::refreshLists);
}

QString PlotPanel::objectTitle() const { return object_.isEmpty() ? QString() : "Объект: " + object_; }

// --- What the reader may change -------------------------------------------------------------------

// After any change the reader made: the history's bounds, the views, and the lists a moment later
// (the click may have come from one of them).
void PlotPanel::chartsChanged() {
    touched_ = true;
    updateKeep();
    forgetColours();
    rebuildCharts();
    QTimer::singleShot(0, this, &PlotPanel::refreshLists);
}

// The lines no longer on any chart give their colours back, so the lines on screen keep the first,
// best-told-apart colours of the palette (the scene's energy keeps its three for good).
void PlotPanel::forgetColours() {
    std::vector<std::string> onScreen;
    for (const Chart& c : charts_) onScreen.insert(onScreen.end(), c.ids.begin(), c.ids.end());
    catalog_.releaseAllBut(onScreen);
}

bool PlotPanel::showsChannel(const std::string& id) const {
    return std::any_of(charts_.begin(), charts_.end(), [&](const Chart& c) { return contains(c.ids, id); });
}

// A line on the first chart of its unit (never on the object's), else on a chart of its own.
void PlotPanel::showChannel(const std::string& id) {
    if (showsChannel(id)) return;
    const QString unit = catalog_.card(id).unit;
    for (Chart& c : charts_)
        if (!c.object && !c.ids.empty() && unitOf(c) == unit) {
            c.ids.push_back(id);
            c.title.clear();
            return chartsChanged();
        }
    charts_.insert(std::find_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.object; }), Chart{{}, {id}, {}, false});
    chartsChanged();
}

// The checklist: on - onto a chart (showChannel); off - off every chart, an emptied chart goes too.
void PlotPanel::setChannelShown(const std::string& id, bool on) {
    if (on) return showChannel(id);
    for (Chart& c : charts_) {
        if (contains(c.ids, id)) c.title.clear();
        eraseId(c.ids, id);
        c.hidden.erase(id);
    }
    charts_.erase(std::remove_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.ids.empty(); }), charts_.end());
    chartsChanged();
}

// The checklist's presets:
//   «Энергия»  - the scene's energy chart, back in front if it was taken away;
//   «Движок»   - a chart of the step's time and its five longest stages right now;
//   «Сбросить» - the charts as they come by themselves: the energy, and the object's if one is selected.
void PlotPanel::applyPreset(const QString& name) {
    if (name == "Энергия") {
        if (!std::any_of(charts_.begin(), charts_.end(), [](const Chart& c) { return c.ids == defaultChannels(); }))
            charts_.insert(charts_.begin(), defaultCharts().front());
    } else if (name == "Движок") {
        std::vector<std::pair<double, std::string>> stages;
        for (const auto& [id, s] : series_) {
            const PlotChannel card = catalog_.card(id);
            if (id != "frame/step ms" && card.group == PlotGroup::Engine && card.unit == "мс" && std::isfinite(s.latest()))
                stages.push_back({s.latest(), id});
        }
        std::sort(stages.rbegin(), stages.rend()); // the longest first
        Chart engine{"Время шага физики по стадиям, мс", {"frame/step ms"}, {}, false};
        for (size_t i = 0; i < stages.size() && i < 5; ++i) engine.ids.push_back(stages[i].second);
        charts_.insert(std::find_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.object; }), engine);
    } else if (name == "Сбросить") {
        charts_ = defaultCharts();
        if (!object_.isEmpty())
            for (const Chart& c : objectCharts(object_)) charts_.push_back(c);
    }
    chartsChanged();
    touched_ = name != "Сбросить";
}

// The «+»: an empty chart after the scene's charts, its list open at once.
void PlotPanel::addChart() {
    const auto at = std::find_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.object; });
    const int chart = int(at - charts_.begin());
    charts_.insert(at, Chart{});
    chartsChanged();
    openChartList(chart);
}

void PlotPanel::openChartList(int chart) {
    if (chart < 0 || chart >= int(charts_.size())) return;
    listChart_ = chart;
    chartList_->setRows(rowsFor(chart));
    TrackChart* view = viewOf(chart);
    if (view) chartList_->popupAt(view, view->titleRect().toRect());
    else chartList_->popupAt(this, QRect(0, 0, width(), 0));
}

// The list's click on a row: this quantity alone on the chart.
void PlotPanel::switchChart(int chart, const std::string& id) {
    if (chart < 0 || chart >= int(charts_.size())) return;
    charts_[size_t(chart)].ids = {id};
    charts_[size_t(chart)].hidden.clear();
    charts_[size_t(chart)].title.clear();
    chartsChanged();
}

// The list's ＋ / ✓: one more line on this chart - or, of another unit, on a new chart right after it
// (never two y-axes) - or one line fewer.
void PlotPanel::setOnChart(int chart, const std::string& id, bool on) {
    if (chart < 0 || chart >= int(charts_.size())) return;
    Chart& c = charts_[size_t(chart)];
    if (!on) {
        eraseId(c.ids, id);
        c.hidden.erase(id);
        c.title.clear();
    } else if (!c.ids.empty() && unitOf(c) != catalog_.card(id).unit) {
        charts_.insert(charts_.begin() + chart + 1, Chart{{}, {id}, {}, c.object});
    } else if (!contains(c.ids, id)) {
        c.ids.push_back(id);
        c.title.clear();
    }
    chartsChanged();
    QTimer::singleShot(0, this, [this] {
        if (chartList_->isVisible()) chartList_->setRows(rowsFor(listChart_)); // the ＋ becomes ✓
    });
}

// A click on a legend entry: that line hidden or shown again (-1: on every chart that has it).
void PlotPanel::toggleLine(int chart, const std::string& id) {
    for (int i = 0; i < int(charts_.size()); ++i) {
        if ((chart >= 0 && i != chart) || !contains(charts_[size_t(i)].ids, id)) continue;
        std::set<std::string>& hidden = charts_[size_t(i)].hidden;
        if (!hidden.erase(id)) hidden.insert(id);
    }
    chartsChanged();
}

// A double click: that line alone; when it is alone already, every line again.
void PlotPanel::isolateLine(int chart, const std::string& id) {
    for (int i = 0; i < int(charts_.size()); ++i) {
        Chart& c = charts_[size_t(i)];
        if ((chart >= 0 && i != chart) || !contains(c.ids, id)) continue;
        const bool alone = std::all_of(c.ids.begin(), c.ids.end(), [&](const std::string& o) { return o == id || c.hidden.count(o); });
        c.hidden.clear();
        if (!alone)
            for (const std::string& o : c.ids)
                if (o != id) c.hidden.insert(o);
    }
    chartsChanged();
}

void PlotPanel::removeChart(int chart) {
    if (chart < 0 || chart >= int(charts_.size())) return;
    charts_.erase(charts_.begin() + chart);
    chartsChanged();
}

// The right click on a chart: what may be done to it, to its lines, to the layout, to the picture.
QMenu* PlotPanel::contextMenu(TrackChart* view, const QPoint& at) {
    const int chart = chartOf(view);
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto item = [&](const char* name, const QString& text, std::function<void()> act) {
        QAction* a = menu->addAction(text);
        a->setObjectName(name);
        connect(a, &QAction::triggered, this, act);
        return a;
    };
    item("plotMenuAdd", "Добавить величину…", [this, chart] {
        if (chart >= 0) return openChartList(chart);
        checklist_->setRows(rowsFor(-1));
        checklist_->popupAt(more_, more_->rect());
    });
    const int k = view->nearestTrack(at);
    const std::string line = k >= 0 ? view->tracks()[size_t(k)].id : std::string();
    item("plotMenuRemoveLine", k >= 0 ? "Убрать линию «" + view->tracks()[size_t(k)].name + "»" : QString("Убрать линию"),
         [this, chart, line] { chart >= 0 ? setOnChart(chart, line, false) : setChannelShown(line, false); })
        ->setEnabled(k >= 0);
    item("plotMenuRemoveChart", "Убрать этот график", [this, chart] { removeChart(chart); })->setEnabled(chart >= 0);
    menu->addSeparator();
    auto* layouts = new QActionGroup(menu);
    const std::pair<const char*, const char*> choices[] = {{"plotMenuOverlay", "Наложить"}, {"plotMenuLanes", "Дорожками"},
                                                           {"plotMenuNormalized", "Нормировать"}};
    for (int i = 0; i < 3; ++i) {
        QAction* a = item(choices[i].first, choices[i].second, [this, i] { setArrangement(Arrangement(i)); });
        a->setCheckable(true);
        a->setChecked(int(arrangement_) == i);
        layouts->addAction(a);
    }
    QAction* whole = item("plotMenuWhole", "Весь прогон, а не последняя минута", [this] { setWholeRun(!wholeRun_); });
    whole->setCheckable(true);
    whole->setChecked(wholeRun_);
    menu->addSeparator();
    item("plotMenuCsv", "Сохранить CSV…", [this] { emit saveCsvRequested(); });
    item("plotMenuCopy", "Скопировать картинку", [view] { QApplication::clipboard()->setImage(view->grab().toImage()); });
    const double t = view->timeAtX(at.x());
    item("plotMenuLab", "Показать этот момент в Лаборатории", [this, t] { emit timeClicked(t); })->setEnabled(frames_ > 0);
    return menu;
}

// --- The lists ------------------------------------------------------------------------------------

// The rows of a list: every recorded quantity and every one on a chart (an object's may not be
// measured yet), each with its card, whether it is on this chart (chart ≥ 0) or anywhere (-1), its
// colour when drawn and its latest value.
std::vector<ChannelList::Row> PlotPanel::rowsFor(int chart) {
    std::set<std::string> ids;
    for (const auto& [id, s] : series_) ids.insert(id);
    for (const Chart& c : charts_) ids.insert(c.ids.begin(), c.ids.end());
    std::vector<ChannelList::Row> rows;
    for (const std::string& id : ids) {
        ChannelList::Row r;
        r.channel = catalog_.card(id);
        r.on = chart >= 0 && chart < int(charts_.size()) ? contains(charts_[size_t(chart)].ids, id) : showsChannel(id);
        if (showsChannel(id)) r.color = catalog_.color(id), r.style = catalog_.lineStyle(id);
        const PlotSeries* s = series(id);
        r.value = s ? formatValue(s->latest()) : "—";
        rows.push_back(r);
    }
    return rows;
}

void PlotPanel::refreshLists() {
    if (checklist_->isVisible()) checklist_->setRows(rowsFor(-1));
    if (chartList_->isVisible()) chartList_->setRows(rowsFor(listChart_));
}

// --- The charts on screen -------------------------------------------------------------------------

QString PlotPanel::unitOf(const Chart& c) { return c.ids.empty() ? QString() : catalog_.card(c.ids.front()).unit; }

// A chart's title in words: its fixed words, else its line's name and unit, else the first line's
// name, how many more, and their unit.
QString PlotPanel::titleOf(const Chart& c) {
    if (!c.title.isEmpty()) return c.title;
    if (c.ids.empty()) return "Выберите величину";
    const PlotChannel first = catalog_.card(c.ids.front());
    const QString unit = first.unit.isEmpty() ? QString() : ", " + first.unit;
    if (c.ids.size() == 1) return first.name + unit;
    return QString("%1 и ещё %2%3").arg(first.name).arg(c.ids.size() - 1).arg(unit);
}

PlotTrack PlotPanel::trackOf(const std::string& id, bool hidden) {
    const PlotChannel card = catalog_.card(id);
    return {id, card.name, card.unit, catalog_.color(id), catalog_.lineStyle(id), series(id), hidden};
}

int PlotPanel::chartOf(const TrackChart* view) const {
    for (size_t i = 0; i < views_.size(); ++i)
        if (views_[i] == view) return viewChart_[i];
    return -1;
}

TrackChart* PlotPanel::viewOf(int chart) const {
    for (size_t i = 0; i < views_.size(); ++i)
        if (viewChart_[i] == chart) return views_[i];
    return nullptr;
}

// A chart's view and what it answers: the hover, a click (the Laboratory), the title's ▾, the
// legend, the right click. chart -1: the normalised chart of every line.
TrackChart* PlotPanel::makeView(int chart) {
    auto* v = new TrackChart;
    v->setObjectName("trackChart");
    v->setNormalized(chart < 0);
    v->setCursorTime(cursor_);
    connect(v, &TrackChart::hovered, this, [this, v](double t, QPoint at, int track) { onHover(v, t, at, track); });
    connect(v, &TrackChart::hoverLeft, this, &PlotPanel::onHoverLeft);
    connect(v, &TrackChart::clicked, this, &PlotPanel::timeClicked);
    connect(v, &TrackChart::titleClicked, this, [this, chart] { openChartList(chart); });
    connect(v, &TrackChart::legendClicked, this, [this, v, chart](int k) { toggleLine(chart, v->tracks()[size_t(k)].id); });
    connect(v, &TrackChart::legendDoubleClicked, this, [this, v, chart](int k) { isolateLine(chart, v->tracks()[size_t(k)].id); });
    connect(v, &TrackChart::menuRequested, this, [this, v](QPoint at) { contextMenu(v, at)->popup(v->mapToGlobal(at)); });
    views_.push_back(v);
    viewChart_.push_back(chart);
    return v;
}

// The charts again, for the charts and the layout:
//   Наложить    - the scene's charts side by side (three to a row), the object's beside them;
//   Дорожками   - every chart under the one before, one set of margins so the time axes line up, the
//                 time labelled under the lowest lane only;
//   Нормировать - one chart of every line, each 0 … 1.
// With an object selected, each part has a quiet header: «Сцена», «Объект: Куб 1».
void PlotPanel::rebuildCharts() {
    if (QWidget* old = scroll_->takeWidget()) old->deleteLater();
    views_.clear();
    viewChart_.clear();
    auto* host = new QWidget;
    host->setObjectName("plotHost");
    const bool lanes = arrangement_ == Arrangement::Lanes;
    QBoxLayout* box = lanes ? static_cast<QBoxLayout*>(new QVBoxLayout(host)) : new QHBoxLayout(host);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(lanes ? 6 : 12);
    if (arrangement_ == Arrangement::Normalized) {
        box->addWidget(normalizedView(), 1);
    } else {
        std::vector<int> scene, object;
        for (int i = 0; i < int(charts_.size()); ++i) (charts_[size_t(i)].object ? object : scene).push_back(i);
        const bool headers = !object.empty();
        box->addWidget(section(headers ? QString("Сцена") : QString(), scene, true), std::max(1, int(scene.size())));
        if (!object.empty()) box->addWidget(section(objectTitle(), object, false), int(object.size()));
    }
    scroll_->setWidget(host);
    refreshCharts();
    showHint();
    if (lanes) emit roomWanted(host->minimumSizeHint().height() + 64); // the lanes and the bar under them
}

// Every line of every chart, each once, on one chart - each scaled to 0 … 1.
TrackChart* PlotPanel::normalizedView() {
    std::vector<PlotTrack> tracks;
    std::set<std::string> seen;
    for (const Chart& c : charts_)
        for (const std::string& id : c.ids)
            if (seen.insert(id).second) tracks.push_back(trackOf(id, c.hidden.count(id) > 0));
    TrackChart* v = makeView(-1);
    v->setTitle("Все линии от 0 до 1 — форма, не величина", false);
    v->setEmptyText("Нет ни одной линии: щёлкните правой кнопкой — «Добавить величину…»");
    v->setTracks(tracks);
    return v;
}

// A part of the charts: its header (when there are two parts), its charts - side by side, three to a
// row, or as lanes one under another - and, in the scene's part, the quiet «+» for one more chart.
QWidget* PlotPanel::section(const QString& header, const std::vector<int>& charts, bool plus) {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(3);
    if (!header.isEmpty()) {
        auto* title = new QLabel(header);
        title->setObjectName("plotSection");
        title->setStyleSheet("color: #8fc1ff; font-weight: 600; padding-left: 4px;");
        col->addWidget(title);
    }
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    const bool lanes = arrangement_ == Arrangement::Lanes;
    const int columns = lanes ? 1 : std::clamp(int(charts.size()), 1, 3);
    for (size_t i = 0; i < charts.size(); ++i) {
        const Chart& c = charts_[size_t(charts[i])];
        std::vector<PlotTrack> tracks;
        for (const std::string& id : c.ids) tracks.push_back(trackOf(id, c.hidden.count(id) > 0));
        TrackChart* v = makeView(charts[i]);
        v->setTitle(titleOf(c));
        v->setEmptyText(c.ids.empty() ? "Щёлкните ▾ у заголовка и выберите, что рисовать"
                                      : "Ждём первых чисел — они приходят, пока сцена идёт");
        v->setTracks(tracks);
        if (lanes) v->setMinimumHeight(96);
        grid->addWidget(v, int(i) / columns, int(i) % columns);
    }
    if (plus) {
        auto* add = new QToolButton;
        add->setObjectName("plotAddChart");
        add->setText("+");
        add->setToolTip("Ещё один график: выбрать, что на нём");
        add->setStyleSheet("QToolButton { color: #8b93a2; border: 1px dashed #3a3f48; border-radius: 6px; background: transparent; font-size: 17px; }"
                           "QToolButton:hover { color: white; border-color: #4096ff; }");
        connect(add, &QToolButton::clicked, this, &PlotPanel::addChart);
        const int rows = std::max(1, (int(charts.size()) + columns - 1) / columns);
        add->setSizePolicy(lanes ? QSizePolicy::Expanding : QSizePolicy::Fixed, lanes ? QSizePolicy::Fixed : QSizePolicy::Expanding);
        if (lanes) add->setFixedHeight(20), grid->addWidget(add, rows, 0);
        else add->setFixedWidth(22), grid->addWidget(add, 0, columns, rows, 1);
    }
    col->addLayout(grid, 1);
    return w;
}

// The span of time on the charts: the whole run while it is short (or when asked for), else the
// last minute, sliding with the run.
std::pair<double, double> PlotPanel::timeWindow() const {
    if (frames_ == 0) return {0.0, 1.0};
    const double from = wholeRun_ || now_ - start_ <= kLiveSeconds ? start_ : now_ - kLiveSeconds;
    return {from, std::max(now_, from + 1e-3)};
}

// The lanes share their margins, so that one moment is one x on every lane; the lowest one labels the time.
void PlotPanel::alignLanes() {
    int left = 0, right = 0;
    for (const TrackChart* v : views_) left = std::max(left, v->neededLeftMargin()), right = std::max(right, v->neededRightMargin());
    for (size_t i = 0; i < views_.size(); ++i) {
        views_[i]->setSharedMargins(left, right);
        views_[i]->setTimeAxisVisible(i + 1 == views_.size());
    }
}

// At most every 40 ms, while frames come: the window slides, every chart takes its new samples, the
// lanes line up again, an open list shows fresh values.
void PlotPanel::refreshCharts() {
    const auto [t0, t1] = timeWindow();
    for (TrackChart* v : views_) {
        v->setWindow(t0, t1);
        v->refresh();
    }
    if (arrangement_ == Arrangement::Lanes) alignLanes();
    if (!checklist_->isVisible() && !chartList_->isVisible()) return;
    std::map<std::string, QString> values;
    for (const auto& [id, s] : series_) values[id] = formatValue(s.latest());
    checklist_->updateValues(values);
    chartList_->updateValues(values);
}

// Before the first frame: one quiet line instead of the charts - nothing to press, nothing to choose.
void PlotPanel::showHint() {
    scroll_->setVisible(frames_ > 0);
    placeholder_->setVisible(frames_ == 0);
}

// --- The cursor and the hover ---------------------------------------------------------------------

void PlotPanel::setCursorTime(double t) {
    cursor_ = t;
    for (TrackChart* v : views_) v->setCursorTime(t);
}

void PlotPanel::clearCursor() { setCursorTime(std::nan("")); }

// The mouse over a chart: the dashed line and the dots on every chart, and the line under the mouse
// in words.
void PlotPanel::onHover(TrackChart* view, double t, QPoint globalPos, int track) {
    for (TrackChart* v : views_) v->setHoverTime(t);
    const QString text = tooltipText(view, t, track);
    if (text.isEmpty()) QToolTip::hideText();
    else QToolTip::showText(globalPos + QPoint(14, 12), text, view);
}

void PlotPanel::onHoverLeft() {
    for (TrackChart* v : views_) v->setHoverTime(std::nan(""));
    QToolTip::hideText();
}

// «Кинетическая энергия: 1,23 Дж в 2,40 с» - the recorded value, also on the normalised chart.
QString PlotPanel::tooltipText(const TrackChart* view, double t, int track) const {
    if (!view || track < 0 || track >= int(view->tracks().size())) return QString();
    const PlotTrack& line = view->tracks()[size_t(track)];
    const double v = line.series ? line.series->valueAt(t) : std::nan("");
    if (!std::isfinite(v)) return QString();
    const QString value = formatValue(v) + (line.unit.isEmpty() ? QString() : " " + line.unit);
    return QString("%1: %2 в %3 с").arg(line.name, value, QString::number(t, 'f', 2).replace('.', ','));
}

// --- The CSV --------------------------------------------------------------------------------------

namespace {

// Channels as a table: the time first, then a column each; a row per moment any of them was
// recorded, an empty cell where one was not. For people («t, с», names with units, a byte-order mark
// so that a spreadsheet reads the Russian) or for programs ("t", the channels' ids).
bool writeTable(const QString& path, const std::vector<std::pair<QString, const PlotSeries*>>& columns, bool people) {
    QFile f(path);
    if (columns.empty() || !f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    std::vector<double> times;
    for (const auto& [head, s] : columns)
        for (size_t i = 0; i < s->size(); ++i) times.push_back(s->time(i));
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    QTextStream out(&f);
    out.setGenerateByteOrderMark(people);
    auto quoted = [](QString s) { return "\"" + s.replace("\"", "\"\"") + "\""; };
    out << (people ? quoted("t, с") : QString("t"));
    for (const auto& [head, s] : columns) out << "," << quoted(head);
    out << "\n";
    std::vector<size_t> at(columns.size(), 0);
    for (double t : times) {
        out << QString::number(t, 'g', 9);
        for (size_t c = 0; c < columns.size(); ++c) {
            const PlotSeries& s = *columns[c].second;
            while (at[c] < s.size() && s.time(at[c]) < t) ++at[c];
            out << ",";
            if (at[c] < s.size() && s.time(at[c]) == t && std::isfinite(s.value(at[c]))) out << QString::number(s.value(at[c]), 'g', 9);
        }
        out << "\n";
    }
    return true;
}

} // namespace

// Every channel (the automation's --csv): first the energy chart's three lines, then the solvers'
// readings, then the rest by id - a row for every simulated frame.
bool PlotPanel::writeCsv(const QString& path) const {
    std::vector<std::string> first = defaultChannels();
    first.insert(first.end(), plotNames_.begin(), plotNames_.end());
    std::vector<std::pair<QString, const PlotSeries*>> columns;
    for (const std::string& id : first)
        if (const PlotSeries* s = series(id)) columns.push_back({QString::fromStdString(id), s});
    for (const auto& [id, s] : series_)
        if (!contains(first, id)) columns.push_back({QString::fromStdString(id), &s});
    return writeTable(path, columns, false);
}

// The lines on the charts (the shown ones, each once), headed with their names and units.
bool PlotPanel::writeVisibleCsv(const QString& path) const {
    std::vector<std::pair<QString, const PlotSeries*>> columns;
    std::set<std::string> seen;
    for (const Chart& c : charts_)
        for (const std::string& id : c.ids) {
            const PlotSeries* s = series(id);
            if (!s || c.hidden.count(id) || !seen.insert(id).second) continue;
            const PlotChannel card = catalog_.card(id);
            columns.push_back({card.unit.isEmpty() ? card.name : card.name + ", " + card.unit, s});
        }
    return writeTable(path, columns, true);
}
