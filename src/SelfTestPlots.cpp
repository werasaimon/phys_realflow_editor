// The plots, the way a newcomer meets them and the way a researcher uses them (see PlotPanel.h):
//   1. zero clicks: a cube made «Твёрдое», ▶ - and a second later the plots are open with the
//      scene's energy drawn; a click on the cube adds its height and speed; nothing selected, they
//      go; not one plot control touched on the way;
//   2. nothing technical on screen: no channel ids in the titles, the legends, the lists (the
//      engine's internals folded under «Движок (для разработчиков)»); the hover speaks in words;
//   3. a falling cube's mechanical energy stays flat until it lands;
//   4. on the chart: the title's ▾ switches the quantity; the right click adds and removes a line;
//      a legend click hides a line, a double click shows it alone;
//   5. «Наложить»: the energy's three lines are one chart, another unit is another chart; colours
//      stay with their lines; «Дорожками»: one time axis and the Laboratory's cursor, both ways;
//      «Нормировать»: every line 0 … 1;
//   6. bounded cost: 30 simulated minutes of a real scene's channels - the memory grows by less than
//      20 MB after the first minute, a paint of twelve lines costs the same at the end as at the start.
// Each check prints one line; with a directory, the pictures plots-1-before.png, plots-2-play.png,
// plots-3-cube.png, plots-dropdown.png, plots-menu.png, plots-overlay.png, plots-lanes.png,
// plots-normalized.png.
#include "SelfTestSupport.h"

#include "LabTimeline.h"
#include "PlotPanel.h"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QSlider>
#include <QTreeWidget>

#include <windows.h>
#include <psapi.h>

#include <cmath>

using namespace rf;
using namespace selftest;

namespace {

struct Plots {
    PlotPanel* panel = nullptr;
    QAction* graphs = nullptr;
    QDockWidget* dock = nullptr;
    LabTimeline* timeline = nullptr;
};

bool newFrames(Viewport* v, int count) { return waitFrames(v, (v->snapshot() ? v->snapshot()->frame : 0) + uint64_t(count), 15000); }

void click(QWidget* w, const QPointF& p) {
    QMouseEvent press(QEvent::MouseButtonPress, p, w->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, p, w->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &press);
    QCoreApplication::sendEvent(w, &release);
    pump(80);
}

void doubleClick(QWidget* w, const QPointF& p) {
    QMouseEvent twice(QEvent::MouseButtonDblClick, p, w->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &twice);
    pump(80);
}

// The window's picture with a popup laid over it where it opened (a popup or a menu is a window of
// its own); `at` in the window's coordinates, else where the popup is.
QImage shotWith(QMainWindow& w, QWidget* popup, QPoint at = QPoint(-1, -1)) {
    QImage shot = windowShot(w);
    if (popup && popup->isVisible()) {
        QPainter p(&shot);
        p.drawImage(at.x() >= 0 ? at : w.mapFromGlobal(popup->mapToGlobal(QPoint(0, 0))), popup->grab().toImage());
    }
    return shot;
}

TrackChart* viewTitled(const Plots& p, const QString& title) {
    for (TrackChart* v : p.panel->views())
        if (v->title() == title) return v;
    return nullptr;
}

bool hasNumbers(const PlotPanel* panel, const std::string& id) {
    const PlotSeries* s = panel->series(id);
    return s && s->size() > 3 && std::isfinite(s->latest());
}

// A point on line k of a chart, for a right click on that line: across the plot, the moment where
// the heights nearest to that line make the widest band - there it stands apart from the others -
// and the middle of that band.
QPointF pointOnLine(const TrackChart* v, size_t k) {
    const QRectF r = v->plotArea();
    QPointF best = r.center();
    double widest = -1;
    for (double x = r.left() + 4; x < r.right() - 4; x += 4) {
        double first = -1, last = -1;
        for (double y = r.top(); y <= r.bottom(); y += 1.0)
            if (v->nearestTrack(QPointF(x, y)) == int(k)) last = y, first = first < 0 ? y : first;
        if (first >= 0 && last - first > widest) widest = last - first, best = QPointF(x, (first + last) / 2);
    }
    return best;
}

// --- 1. Zero clicks -------------------------------------------------------------------------------

const QString kEnergyTitle = "Энергия сцены — движения, высоты, полная (Дж)";

// A cube 3 m above the floor, tilted, so that it lands on an edge and tumbles: energy of every kind.
void fallingCube(QMainWindow& w, SceneBuilder& b) {
    b.newScene();
    pump(100);
    b.createActions()[0]->trigger(); // Куб
    b.editEntity(last(b).id, [](Entity& e) { e.position = Vector3(0.0f, 3.0f, 0.0f); e.rotationDeg = Vector3(25, 30, 10); });
    clickRole(w, RoleIcon::Rigid);
    b.select(0); // made and set up; now the newcomer looks at the scene
    pump(200);
}

void testZeroClicks(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const Plots& p, const QString& dir) {
    p.graphs->setChecked(false); // a fresh start: the plots closed, as in a new window
    fallingCube(w, b);
    const bool closedBefore = !p.dock->isVisible();
    if (!dir.isEmpty()) windowShot(w).save(dir + "/plots-1-before.png");
    trigger(w, "actionPlay"); // ▶
    pump(1000);
    TrackChart* energy = viewTitled(p, kEnergyTitle);
    const bool drawn = energy && energy->isVisible() && energy->tracks().size() == 3 && hasNumbers(p.panel, "scene/kinetic energy") &&
                       hasNumbers(p.panel, "scene/potential energy") && hasNumbers(p.panel, "scene/mechanical energy");
    std::printf("  before ▶ the plots are %s; a second after ▶: %s, «%s» %s\n", closedBefore ? "closed" : "OPEN",
                p.dock->isVisible() ? "open" : "closed", qPrintable(kEnergyTitle), drawn ? "drawn" : "MISSING");
    c.check(closedBefore && p.dock->isVisible() && drawn,
            "zero clicks: a second after the first ▶ the plots are open and the scene's energy (3 lines) is drawn");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/plots-2-play.png");

    const auto s = v->snapshot();
    rf::Vector3 cube(0.0f);
    for (const auto& body : s->bodies)
        if (body.movable) cube = body.pos;
    click(v, toPoint(v->gizmoView().project(cube)));
    newFrames(v, 20);
    pump(200);
    TrackChart* height = viewTitled(p, "Высота, м");
    TrackChart* speed = viewTitled(p, "Скорость, м/с");
    const bool objectDrawn = height && speed && height->tracks().size() == 1 && hasNumbers(p.panel, height->tracks()[0].id) &&
                             hasNumbers(p.panel, speed->tracks()[0].id);
    std::printf("  a click on the cube: «%s» with «Высота, м» and «Скорость, м/с» %s\n", qPrintable(p.panel->objectTitle()),
                objectDrawn ? "drawn" : "MISSING");
    c.check(p.panel->objectTitle().startsWith("Объект: ") && objectDrawn, "zero clicks: a click on the cube adds its height and speed charts");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/plots-3-cube.png");
    b.select(0);
    pump(150);
    c.check(p.panel->objectTitle().isEmpty() && !viewTitled(p, "Высота, м") && viewTitled(p, kEnergyTitle),
            "zero clicks: nothing selected, the object's charts go and the energy stays");
}

// --- 2. Words, not ids ----------------------------------------------------------------------------

void testNoIds(Checker& c, const Plots& p) {
    static const QRegularExpression id("[A-Za-z]+/[A-Za-z]"); // "rigid/kinetic", "gas/max div u"
    std::vector<QString> texts = {p.panel->placeholder()->text(), p.panel->objectTitle()};
    for (const TrackChart* v : p.panel->views()) {
        texts.push_back(v->title());
        for (const PlotTrack& t : v->tracks()) texts.push_back(t.name + " " + t.unit);
    }
    ChannelList* list = p.panel->chartList();
    p.panel->openChartList(0);
    pump(80);
    for (const QString& t : list->shownTexts()) texts.push_back(t);
    QTreeWidgetItem* engine = list->rowOf("frame/step ms");
    const bool folded = engine && engine->parent()->text(0).startsWith("Движок (для разработчиков)") && !engine->parent()->isExpanded();
    list->hide();
    TrackChart* energy = viewTitled(p, kEnergyTitle);
    const QString hover = energy ? p.panel->tooltipText(energy, energy->timeRange().first + 0.5, 0) : QString();
    texts.push_back(hover);
    QString leak;
    for (const QString& t : texts)
        if (id.match(t).hasMatch()) leak = t;
    std::printf("  %zu texts on screen, an id among them: %s; the hover: «%s»\n", texts.size(), leak.isEmpty() ? "none" : qPrintable(leak),
                qPrintable(hover));
    static const QRegularExpression words("^Кинетическая энергия: -?[0-9]+(,[0-9]+)?(·10⁻?[⁰¹²³⁴⁵⁶⁷⁸⁹]+)? Дж в [0-9]+,[0-9]{2} с$");
    c.check(leak.isEmpty() && folded && words.match(hover).hasMatch(),
            "no ids on screen: titles, legends and lists in Russian words (the engine's internals folded away); "
            "the hover says «Кинетическая энергия: 1,23 Дж в 2,40 с»");
}

// --- 3. The energy of a free fall -----------------------------------------------------------------

// The free fall proper: from the first measured frame while the cube is still above a fifth of its
// height - far from the floor, where the solver's contacts «на подлёте» (1 cm) begin to brake it.
void testFreeFall(Checker& c, const Plots& p) {
    const PlotSeries* energy = p.panel->series("scene/mechanical energy");
    const PlotSeries* height = p.panel->series("scene/potential energy");
    if (!energy || !height || energy->size() < 10 || height->size() != energy->size())
        return c.check(false, "the free fall was recorded");
    size_t falling = 1;
    while (falling < height->size() && height->value(falling) > 0.2 * height->value(0)) ++falling;
    const double e0 = energy->value(0);
    double drift = 0;
    for (size_t i = 0; i < falling; ++i) drift = std::max(drift, std::fabs(energy->value(i) - e0));
    const double share = e0 > 0 ? drift / e0 : 1;
    std::printf("  free fall: %zu frames from 3 m down to 0,6 m, E = %.4f J, the largest drift %.5f J (%.3f %%)\n", falling, e0, drift,
                100 * share);
    c.check(falling > 20 && e0 > 0 && share < 0.01, "a falling cube's mechanical energy stays flat (within 1 %) while it falls");
}

// --- 4. On the chart ------------------------------------------------------------------------------

void testTitleList(Checker& c, QMainWindow& w, const Plots& p, const QString& dir) {
    TrackChart* energy = viewTitled(p, kEnergyTitle);
    if (!energy) return c.check(false, "the energy chart is there");
    click(energy, energy->titleRect().center());
    ChannelList* list = p.panel->chartList();
    const bool opened = list->isVisible();
    list->search()->setText("импульс");
    pump(60);
    if (!dir.isEmpty()) shotWith(w, list).save(dir + "/plots-dropdown.png");
    list->search()->clear();
    list->choose("rigid/momentum"); // a click on the row
    pump(80);
    const bool switched = p.panel->charts()[0].ids == std::vector<std::string>{"rigid/momentum"} && p.panel->views()[0]->tracks().size() == 1;
    std::printf("  the title's ▾ %s the list; after a click on «импульс» the chart is «%s»\n", opened ? "opens" : "does NOT open",
                qPrintable(p.panel->views()[0]->title()));
    c.check(opened && switched, "a click on a chart's title opens its list; a row there switches the chart to that quantity");
    p.panel->applyPreset("Сбросить");
    pump(80);
}

void testContextMenu(Checker& c, QMainWindow& w, const Plots& p, const QString& dir) {
    TrackChart* energy = viewTitled(p, kEnergyTitle);
    if (!energy) return c.check(false, "the energy chart is there");
    const QPoint at = pointOnLine(energy, 2).toPoint();
    QMenu* menu = p.panel->contextMenu(energy, at); // drawn where it opens, not shown: a shown menu
    menu->adjustSize();                              // closes itself when another program is in front
    if (!dir.isEmpty()) {
        QImage shot = windowShot(w);
        QPainter painter(&shot);
        const QPoint inWindow = energy->mapTo(&w, at);
        painter.drawImage(QPoint(inWindow.x(), std::max(0, inWindow.y() - menu->height())), menu->grab().toImage());
        painter.end();
        shot.save(dir + "/plots-menu.png");
    }
    menu->findChild<QAction*>("plotMenuAdd")->trigger();
    delete menu;
    pump(80);
    p.panel->chartList()->toggle("rigid/rotational energy"); // the ＋ on its row
    pump(80);
    const size_t added = p.panel->charts()[0].ids.size();
    p.panel->chartList()->hide();
    TrackChart* chart = p.panel->views()[0];
    const auto& lines = chart->tracks();
    const size_t k = size_t(std::find_if(lines.begin(), lines.end(), [](const PlotTrack& t) { return t.id == "rigid/rotational energy"; }) - lines.begin());
    QMenu* again = p.panel->contextMenu(chart, pointOnLine(chart, k).toPoint());
    QAction* remove = again->findChild<QAction*>("plotMenuRemoveLine");
    const QString removeText = remove ? remove->text() : QString();
    if (remove) remove->trigger();
    delete again;
    pump(80);
    std::printf("  right click: «Добавить величину…» -> %zu lines; «%s» -> %zu lines\n", added, qPrintable(removeText),
                p.panel->charts()[0].ids.size());
    c.check(added == 4 && removeText.contains("Энергия вращения", Qt::CaseInsensitive) && p.panel->charts()[0].ids.size() == 3,
            "the right click adds a line («Добавить величину…») and removes the line under the mouse («Убрать линию»)");
    p.panel->applyPreset("Сбросить"); // the energy chart with its own title again
    pump(80);
}

void testLegend(Checker& c, const Plots& p) {
    TrackChart* chart = viewTitled(p, kEnergyTitle);
    if (!chart) return c.check(false, "the energy chart is there");
    click(chart, chart->legendRect(1).center());
    chart = viewTitled(p, kEnergyTitle);
    const bool hidden = chart->tracks()[1].hidden && !chart->tracks()[0].hidden;
    click(chart, chart->legendRect(1).center());
    chart = viewTitled(p, kEnergyTitle);
    const bool back = !chart->tracks()[1].hidden;
    doubleClick(chart, chart->legendRect(2).center());
    chart = viewTitled(p, kEnergyTitle);
    const bool alone = chart->tracks()[0].hidden && chart->tracks()[1].hidden && !chart->tracks()[2].hidden;
    doubleClick(chart, chart->legendRect(2).center());
    chart = viewTitled(p, kEnergyTitle);
    const bool all = !chart->tracks()[0].hidden && !chart->tracks()[1].hidden && !chart->tracks()[2].hidden;
    std::printf("  legend: a click hides %s, again shows %s; a double click isolates %s, again all %s\n", hidden ? "yes" : "no",
                back ? "yes" : "no", alone ? "yes" : "no", all ? "yes" : "no");
    c.check(hidden && back && alone && all, "a click on a legend entry hides and shows its line; a double click shows it alone, and back");
}

// --- 5. Layouts, colours, the Laboratory ----------------------------------------------------------

QColor colorOf(const Plots& p, const std::string& id) {
    for (const TrackChart* v : p.panel->views())
        for (const PlotTrack& t : v->tracks())
            if (t.id == id) return t.color;
    return QColor();
}

void testOverlay(Checker& c, QMainWindow& w, const Plots& p, const QString& dir) {
    p.panel->setArrangement(PlotPanel::Arrangement::Overlay);
    pump(50);
    const bool one = p.panel->views().size() == 1 && p.panel->views()[0]->tracks().size() == 3;
    const QColor kinetic = colorOf(p, "scene/kinetic energy"), total = colorOf(p, "scene/mechanical energy");
    p.panel->setChannelShown("rigid/momentum", true);
    pump(50);
    const bool two = p.panel->views().size() == 2;
    std::printf("  overlay: the energy's 3 lines -> %s; with the momentum (Н·с) -> %zu charts\n", one ? "1 chart" : "NOT one chart",
                p.panel->views().size());
    c.check(one && two, "«Наложить»: three energies (one unit) are one chart of three lines; another unit is a second chart");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/plots-overlay.png");
    p.panel->setChannelShown("rigid/momentum", false);
    p.panel->setChannelShown("rigid/angular momentum", true);
    pump(50);
    c.check(colorOf(p, "scene/kinetic energy") == kinetic && colorOf(p, "scene/mechanical energy") == total &&
                colorOf(p, "rigid/angular momentum") != kinetic && colorOf(p, "rigid/angular momentum") != total,
            "colours stay with their lines when another line is switched on or off");
}

void testLanes(Checker& c, QMainWindow& w, SceneBuilder& b, const Plots& p, const QString& dir) {
    p.panel->setArrangement(PlotPanel::Arrangement::Lanes);
    pump(100);
    const std::vector<TrackChart*> lanes = p.panel->views();
    bool shared = lanes.size() == 2;
    for (size_t i = 1; shared && i < lanes.size(); ++i)
        shared = lanes[i]->timeRange() == lanes[0]->timeRange() && std::fabs(lanes[i]->xAtTime(1.0) - lanes[0]->xAtTime(1.0)) < 1e-6;
    c.check(shared, "«Дорожками»: the charts stacked on one time axis (same span, same margins)");

    trigger(w, "actionLaboratory");
    pump(200);
    const int k = p.timeline->count() / 3;
    p.timeline->findChild<QSlider*>("labScrubber")->setValue(k); // the scrubber taken: the scene pauses
    pump(100);
    const double at = p.timeline->frameAt(k)->time;
    bool cursor = !p.panel->views().empty();
    for (const TrackChart* lane : p.panel->views()) cursor = cursor && lane->cursorTime() == at;
    c.check(cursor, "the Laboratory's scrubber moves the plots' cursor on every lane");

    const int target = p.timeline->count() * 2 / 3;
    TrackChart* lane = p.panel->views().front();
    click(lane, QPointF(lane->xAtTime(p.timeline->frameAt(target)->time), lane->plotArea().center().y()));
    const int landed = p.timeline->position();
    std::printf("  the scrubber at frame %d: the cursor there on every lane: %s; a click at frame %d's moment -> frame %d\n", k,
                cursor ? "yes" : "no", target, landed);
    c.check(std::abs(landed - target) <= 1 && p.panel->views().front()->cursorTime() == p.timeline->frameAt(landed)->time,
            "a click on a plot takes the Laboratory's timeline to that moment");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/plots-lanes.png");
    p.timeline->goLive();
    pump(100);
    c.check(std::isnan(p.panel->views().front()->cursorTime()), "«Вживую» takes the cursor off the plots");
    b.pause();
    trigger(w, "actionLaboratory");
    pump(100);
}

void testNormalized(Checker& c, QMainWindow& w, const Plots& p, const QString& dir) {
    p.panel->setArrangement(PlotPanel::Arrangement::Normalized);
    pump(100);
    bool fits = p.panel->views().size() == 1 && p.panel->views()[0]->tracks().size() == 4;
    const TrackChart* chart = fits ? p.panel->views()[0] : nullptr;
    for (size_t k = 0; chart && k < chart->tracks().size(); ++k) {
        double lo = INFINITY, hi = -INFINITY;
        const auto [t0, t1] = chart->timeRange();
        for (int i = 0; i <= 400; ++i)
            if (const double s = chart->shownValueAt(k, t0 + (t1 - t0) * i / 400.0); std::isfinite(s)) lo = std::min(lo, s), hi = std::max(hi, s);
        fits = fits && lo >= -1e-9 && hi <= 1 + 1e-9 && (hi - lo > 0.9 || hi == lo);
    }
    c.check(fits, "«Нормировать»: every line on one chart, each scaled to 0 … 1");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/plots-normalized.png");
    p.panel->setArrangement(PlotPanel::Arrangement::Overlay);
    p.panel->applyPreset("Сбросить");
}

// --- 6. Thirty minutes ----------------------------------------------------------------------------

double privateMegabytes() {
    PROCESS_MEMORY_COUNTERS_EX m{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof(m));
    return double(m.PrivateUsage) / 1048576.0;
}

// A real scene's channels (the last frame's Probe and measurements), fast-forwarded through 30
// simulated minutes into a panel of their own with twelve lines on it: the memory after the first
// minute and at the end, and the paint of the whole panel, averaged over the first and the last minute.
void testThirtyMinutes(Checker& c, Viewport* v) {
    const auto frame = v->snapshot();
    if (!frame) return c.check(false, "a frame to take the channels from");
    PlotPanel panel;
    panel.resize(1500, 230);
    rf::Probe::Snapshot probe = frame->probe;
    std::vector<rf::Measurement> measured = frame->measurements;
    for (const rf::Measurement& m : measured) // the watched cube's name, as its channels carry it
        if (m.info.group == rf::ChannelGroup::Object && m.info.id.size() > 7 && m.info.id.compare(m.info.id.size() - 7, 7, "/height") == 0)
            panel.showObject(QString::fromStdString(m.info.id.substr(0, m.info.id.size() - 7)));
    double firstPaint = 0, lastPaint = 0, memoryAtOne = 0;
    int paints = 0;
    for (int i = 0; i < 30 * 3600; ++i) {
        const double t = i / 60.0;
        for (size_t k = 0; k < probe.channels.size(); ++k) probe.channels[k].value = std::sin(t * (0.1 + 0.01 * double(k))) + 0.01 * std::sin(t * 41.0);
        for (size_t k = 0; k < measured.size(); ++k) measured[k].value = 10 + std::sin(t * (0.2 + 0.03 * double(k)));
        panel.append(t, {}, probe.channels, measured);
        if (i == 3600 - 1) // the first minute: twelve lines on the charts from here on
            for (size_t k = 0; k < probe.channels.size() && panel.views().size() < 6; k += 7) panel.showChannel(probe.channels[k].name);
        if (i % 120 != 0) continue; // a paint every 2 simulated seconds
        QElapsedTimer clock;
        clock.start();
        panel.refreshCharts(); // what the panel's 40 ms timer does
        panel.grab();
        const double ms = double(clock.nsecsElapsed()) / 1e6;
        if (i < 3600 * 2 && i >= 3600) firstPaint += ms, ++paints;
        if (i >= 29 * 3600) lastPaint += ms;
        if (i == 3600 * 2 - 120) firstPaint /= std::max(1, paints), paints = 0, memoryAtOne = privateMegabytes();
    }
    lastPaint /= 30.0;
    const double memoryAtEnd = privateMegabytes();
    size_t lines = 0;
    for (const TrackChart* view : panel.views()) lines += view->tracks().size();
    std::printf("  30 simulated minutes, %zu channels, %zu lines on %zu charts: memory %.1f MB after the first minutes, %.1f MB at the end "
                "(+%.1f MB); the history holds %.1f MB; a paint %.2f ms at the start, %.2f ms at the end\n",
                probe.channels.size() + measured.size(), lines, panel.views().size(), memoryAtOne, memoryAtEnd,
                memoryAtEnd - memoryAtOne, double(panel.bytes()) / 1048576.0, firstPaint, lastPaint);
    c.check(lines >= 10 && memoryAtEnd - memoryAtOne < 20.0, "30 minutes of a scene: the memory grows by less than 20 MB after the first minute");
    c.check(lastPaint < std::max(2.0 * firstPaint, firstPaint + 5.0) && lastPaint < 40.0,
            "30 minutes of a scene: a paint of ten lines and more costs the same at the end as at the start (under 40 ms)");
}

} // namespace

int runPlotTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    Plots p{w.findChild<PlotPanel*>(), w.findChild<QAction*>("actionGraphs"), w.findChild<QDockWidget*>("resultsDock"),
            v->findChild<LabTimeline*>()};
    if (!p.panel || !p.graphs || !p.dock || !p.timeline) {
        c.check(false, "the window has the plots, their action and dock, the Laboratory's timeline");
        return c.failures;
    }
    testZeroClicks(c, w, b, v, p, shotsDir);
    newFrames(v, 60);
    b.pause();
    pump(200);
    testNoIds(c, p);
    testFreeFall(c, p);
    testTitleList(c, w, p, shotsDir);
    testContextMenu(c, w, p, shotsDir);
    testLegend(c, p);
    testOverlay(c, w, p, shotsDir);
    b.play();
    newFrames(v, 30);
    testLanes(c, w, b, p, shotsDir);
    testNormalized(c, w, p, shotsDir);
    testThirtyMinutes(c, v);
    b.stop();
    pump(200);
    return c.failures;
}
