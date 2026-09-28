// The Laboratory's live profiler (see ProfilerBar.h): the stages of the last frame as one stacked bar,
// the parts of the engine in words under it, and the whole step over the last 5 seconds.
#include "ProfilerBar.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

constexpr double kBudgetMs = 1000.0 / 60.0; // 60 frames a second
constexpr qint64 kHistoryMs = 5000;         // the sparkline's 5 seconds
const QColor kText(223, 227, 232), kMuted(139, 147, 162), kTrack(44, 48, 56), kGround(35, 38, 44);

// The parts of the engine and their colours: Okabe & Ito (2008), the palette colour-blind readers
// tell apart - sky blue, orange, bluish green, reddish purple; and a neutral grey for the part of the
// step no timer covers (the frame's own work between the stages).
struct Part { const char* prefix; QString name; QColor color; };
constexpr size_t kOther = 3, kUntimed = 4; // indices into parts()
const std::vector<Part>& parts() {
    static const std::vector<Part> p = {{"rigid", "Твёрдые тела", QColor(86, 180, 233)},
                                        {"particles", "Частицы", QColor(230, 159, 0)},
                                        {"gas", "Газ", QColor(0, 158, 115)},
                                        {"", "Прочее", QColor(204, 121, 167)},
                                        {"", "Вне замеров", QColor(96, 104, 116)}};
    return p;
}

// "rigid/solve ms" -> «решатель»: the stage's name in words (the channel itself when it is new).
// The scene's own stages ("scene/coupling ms", "scene/controllers ms") belong to no solver: they
// go to «Прочее» (partOf), under their words here.
QString stageName(const std::string& channel) {
    static const std::map<std::string, QString> names = {
        {"collide", "столкновения"},   {"solve", "решатель"},        {"integrate", "интегрирование"},
        {"ccd", "непрерывные столкн."}, {"islands", "острова сна"},   {"debug draw", "отладочная графика"},
        {"neighbors", "соседи"},       {"density", "плотность"},     {"contacts", "контакты"},
        {"cloth", "ткань"},            {"emit", "испускание"},       {"predict", "прогноз положений"},
        {"bodies", "связь с телами"},  {"velocity", "скорости"},     {"heat", "тепло"},
        {"advect", "перенос"},         {"diffuse", "вязкость"},      {"forces", "силы"},
        {"mhd", "МГД"},                {"pressure", "давление"},     {"solids", "тела в газе"},
        {"sources", "источники"},      {"diagnostics", "диагностика"}, {"surface loads", "нагрузки на поверхность"},
        {"coupling", "связь частей сцены"}, {"controllers", "управление сценой"}};
    std::string word = channel.substr(channel.find('/') + 1);
    if (word.size() > 3 && word.compare(word.size() - 3, 3, " ms") == 0) word.resize(word.size() - 3);
    const auto it = names.find(word);
    return it != names.end() ? it->second : QString::fromStdString(channel);
}

const Part& partOf(const std::string& channel) {
    for (const Part& p : parts())
        if (*p.prefix && channel.rfind(std::string(p.prefix) + "/", 0) == 0) return p;
    return parts()[kOther];
}

// Milliseconds with as many digits as the size asks for: 0,08 мс, 3,4 мс, 27 мс.
QString ms(double v) {
    const int digits = v < 1 ? 2 : v < 10 ? 1 : 0;
    return QString::number(v, 'f', digits).replace('.', ',') + " мс";
}

} // namespace

ProfilerBar::ProfilerBar(QWidget* parent) : QWidget(parent) {
    setObjectName("profilerBar");
    setMouseTracking(true);
    setToolTip("Куда уходит время шага физики, по частям. Бюджет кадра — 16,7 мс: столько есть у кадра, "
               "чтобы сцена шла со скоростью 60 кадров/с. Правый щелчок по отрезку — «Построить график»");
    clock_.start();
}

// 1. The stages: every timer of the frame, in the order of the parts, the longest first inside a part.
// 2. What no timer covers, grey at the end: the bar adds up to the whole step, honestly.
// 3. The whole step for the sparkline; samples older than 5 s go.
void ProfilerBar::addFrame(const rf::Probe::Snapshot& probe) {
    stages_.clear();
    for (const rf::Probe::Channel& c : probe.channels)
        if (c.kind == rf::Probe::Kind::TimerMs && c.value > 0.0)
            stages_.push_back({c.name, partOf(c.name).name, stageName(c.name), c.value, partOf(c.name).color});
    auto rank = [](const Stage& s) {
        for (size_t i = 0; i < parts().size(); ++i)
            if (parts()[i].name == s.part) return int(i);
        return int(parts().size());
    };
    std::stable_sort(stages_.begin(), stages_.end(), [&](const Stage& a, const Stage& b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : a.ms > b.ms;
    });
    for (size_t i = 1; i < stages_.size(); ++i) // neighbours of one part: every second one a shade darker
        if (stages_[i].part == stages_[i - 1].part && stages_[i - 1].color == partOf(stages_[i - 1].channel).color)
            stages_[i].color = stages_[i].color.darker(128);
    stepMs_ = probe.value("frame/step ms", 0.0);
    double timed = 0;
    for (const Stage& stage : stages_) timed += stage.ms;
    if (stepMs_ > timed * 1.02 + 1e-3) {
        const Part& untimed = parts()[kUntimed];
        stages_.push_back({"frame/step ms", untimed.name, "между замерами", stepMs_ - timed, untimed.color});
    }
    const qint64 now = clock_.elapsed();
    history_.push_back({now, stepMs_});
    while (!history_.empty() && now - history_.front().first > kHistoryMs) history_.pop_front();
    update();
}

void ProfilerBar::clear() {
    stages_.clear();
    history_.clear();
    stepMs_ = 0;
    update();
}

QRectF ProfilerBar::barRect() const { return QRectF(0, 22, width() - 1, 20); }
QRectF ProfilerBar::sparkRect() const { return QRectF(0, 76, width() - 1, height() - 80); }

// The bar's scale: the whole step fills it, so the stages' shares read even when the step is quick
// (how quick it is, the words above the bar say); a step over the frame's budget shows where it ends.
static double barScale(const std::vector<ProfilerBar::Stage>& stages, double stepMs) {
    double sum = 0;
    for (const auto& s : stages) sum += s.ms;
    return std::max({sum, stepMs, 1e-6});
}

// The step in words: its time and its share of one frame's budget (16,7 ms: 60 frames a second).
static QString headline(double stepMs) {
    if (stepMs <= 0) return "Шаг физики: ждём первого шага";
    const double share = 100.0 * stepMs / kBudgetMs;
    const QString part = share < 1 ? QString("меньше 1 %") : QString("%1 %").arg(int(std::lround(share)));
    return "Шаг физики: " + ms(stepMs) + " · " + part + " бюджета кадра";
}

int ProfilerBar::stageAt(const QPointF& p) const {
    const QRectF r = barRect();
    if (!r.contains(p)) return -1;
    const double scale = barScale(stages_, stepMs_);
    double x = r.left();
    for (size_t i = 0; i < stages_.size(); ++i) {
        const double w = stages_[i].ms / scale * r.width();
        if (p.x() >= x && p.x() < x + w) return int(i);
        x += w;
    }
    return -1;
}

void ProfilerBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(kText);
    p.drawText(QRectF(0, 0, width(), 18), Qt::AlignLeft | Qt::AlignVCenter, headline(stepMs_));
    p.setFont(font());
    paintBar(p);
    paintLegend(p);
    paintSparkline(p);
}

// The words inside a segment: its name and time, else the name alone, else nothing - never cut.
static QString segmentLabel(const QFontMetrics& metrics, const ProfilerBar::Stage& s, double room) {
    const QString full = s.name + " " + ms(s.ms);
    if (metrics.horizontalAdvance(full) <= room) return full;
    return metrics.horizontalAdvance(s.name) <= room ? s.name : QString();
}

// 1. The track. 2. Every stage as a segment, a 2 px gap of the ground between neighbours.
// 3. The 60-frames mark, when the step is longer than that. 4. The stage's name inside a segment
// wide enough for it.
void ProfilerBar::paintBar(QPainter& p) {
    const QRectF r = barRect();
    p.setPen(Qt::NoPen);
    p.setBrush(kTrack);
    p.drawRoundedRect(r, 4, 4);
    const double scale = barScale(stages_, stepMs_);
    double x = r.left();
    for (const Stage& s : stages_) {
        const double w = s.ms / scale * r.width();
        const QRectF seg(x, r.top(), std::max(0.0, w - 2.0), r.height());
        p.setBrush(s.color);
        p.drawRect(seg);
        const QString label = segmentLabel(p.fontMetrics(), s, seg.width() - 6);
        if (!label.isEmpty()) {
            p.setPen(QColor(20, 22, 26));
            p.drawText(seg.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
            p.setPen(Qt::NoPen);
        }
        x += w;
    }
    if (kBudgetMs >= scale) return;
    const double budgetX = r.left() + kBudgetMs / scale * r.width();
    p.setPen(QPen(kText, 1.0, Qt::DashLine));
    p.drawLine(QPointF(budgetX, r.top() - 3), QPointF(budgetX, r.bottom() + 3));
}

// The parts that took time this frame, in words, each with its colour: identity never by colour alone.
void ProfilerBar::paintLegend(QPainter& p) {
    std::map<QString, double> byPart;
    for (const Stage& s : stages_) byPart[s.part] += s.ms;
    double x = 0;
    const double y = 48;
    for (const auto& part : parts()) {
        const auto it = byPart.find(part.name);
        if (it == byPart.end()) continue;
        const QString text = part.name + " " + ms(it->second);
        p.setPen(Qt::NoPen);
        p.setBrush(part.color);
        p.drawRoundedRect(QRectF(x, y + 4, 9, 9), 2, 2);
        p.setPen(kMuted);
        const double w = p.fontMetrics().horizontalAdvance(text);
        p.drawText(QPointF(x + 13, y + 13), text);
        x += 13 + w + 12;
    }
}

// The whole step over the last 5 s: a 2 px line, the 60-frames line dashed, the last value a dot.
void ProfilerBar::paintSparkline(QPainter& p) {
    const QRectF r = sparkRect();
    p.setPen(Qt::NoPen);
    p.setBrush(kGround);
    p.drawRoundedRect(r, 4, 4);
    if (history_.size() < 2) return;
    double top = kBudgetMs * 1.5;
    for (const auto& h : history_) top = std::max(top, h.second * 1.1);
    const qint64 now = history_.back().first;
    auto at = [&](qint64 t, double v) {
        return QPointF(r.right() - double(now - t) / kHistoryMs * r.width(), r.bottom() - v / top * r.height());
    };
    const double budgetY = at(now, kBudgetMs).y();
    p.setPen(QPen(kMuted, 1.0, Qt::DashLine));
    p.drawLine(QPointF(r.left(), budgetY), QPointF(r.right(), budgetY));
    p.drawText(QPointF(r.left() + 4, budgetY - 3), "60 кадров/с");
    QPainterPath line;
    line.moveTo(at(history_.front().first, history_.front().second));
    for (const auto& h : history_) line.lineTo(at(h.first, h.second));
    p.setPen(QPen(parts().front().color, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(line);
    p.setPen(Qt::NoPen);
    p.setBrush(kText);
    p.drawEllipse(at(now, history_.back().second), 3.0, 3.0);
    p.setPen(kMuted);
    p.drawText(r.adjusted(0, 2, -4, 0), Qt::AlignRight | Qt::AlignTop, "шаг за 5 с");
}

void ProfilerBar::mouseMoveEvent(QMouseEvent* e) {
    const int i = stageAt(e->position());
    if (i < 0) return QToolTip::hideText();
    const Stage& s = stages_[size_t(i)];
    const double share = stepMs_ > 0 ? 100.0 * s.ms / stepMs_ : 0.0;
    QToolTip::showText(e->globalPosition().toPoint(),
                       QString("%1 · %2: %3 (%4 % шага)\nканал %5 — правый щелчок: график")
                           .arg(s.part, s.name, ms(s.ms)).arg(int(share + 0.5)).arg(QString::fromStdString(s.channel)),
                       this);
}

void ProfilerBar::contextMenuEvent(QContextMenuEvent* e) {
    const int i = stageAt(e->pos());
    const std::string channel = i >= 0 ? stages_[size_t(i)].channel : std::string("frame/step ms");
    const QString what = i >= 0 ? stages_[size_t(i)].part + " · " + stages_[size_t(i)].name : QString("весь шаг");
    QMenu menu(this);
    QAction* plot = menu.addAction("Построить график: " + what);
    if (menu.exec(e->globalPos()) == plot) emit plotRequested(QString::fromStdString(channel));
}
