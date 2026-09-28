// The Laboratory's timeline (see LabTimeline.h): the ring of kept frames and the scrubber strip.
#include "LabTimeline.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QToolButton>

#include <algorithm>

namespace {

// «1 кадр», «3 кадра», «26 кадров»: the Russian plural of the count.
QString framesWord(int n) {
    const int last = n % 10, lastTwo = n % 100;
    if (last == 1 && lastTwo != 11) return QString("%1 кадр").arg(n);
    if (last >= 2 && last <= 4 && (lastTwo < 12 || lastTwo > 14)) return QString("%1 кадра").arg(n);
    return QString("%1 кадров").arg(n);
}

// «▶|» and «|◀»: a triangle and a bar, drawn at twice the size for sharp edges (a font's ◀ is a
// tiny glyph in some fonts). The backward one is the forward one mirrored.
QIcon stepIcon(bool forward) {
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xcf, 0xd8, 0xe3));
    if (!forward) p.translate(28, 0), p.scale(-1, 1);
    p.drawPolygon(QPolygonF({QPointF(7, 6), QPointF(19, 14), QPointF(7, 22)}));
    p.drawRect(QRectF(19, 6, 3, 16));
    p.end();
    pixmap.setDevicePixelRatio(2.0);
    return QIcon(pixmap);
}

} // namespace

LabTimeline::LabTimeline(QWidget* view) : QFrame(view), view_(view) {
    setObjectName("labTimeline");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(10, 5, 8, 5);
    row->setSpacing(8);
    back_ = new QToolButton;
    back_->setObjectName("labStep");
    back_->setIcon(stepIcon(false));
    back_->setToolTip("Кадр назад (сцена встанет на паузу)");
    forward_ = new QToolButton;
    forward_->setObjectName("labStep");
    forward_->setIcon(stepIcon(true));
    forward_->setToolTip("Кадр вперёд");
    slider_ = new QSlider(Qt::Horizontal);
    slider_->setObjectName("labScrubber");
    slider_->setToolTip("Потяните — и вид покажет любой из сохранённых кадров, точно как он был");
    label_ = new QLabel;
    label_->setObjectName("labTimelineText");
    live_ = new QPushButton("● Вживую");
    live_->setObjectName("labLive");
    live_->setToolTip("Назад к последнему кадру: сцена пойдёт дальше, если шла");
    live_->setCursor(Qt::PointingHandCursor);
    row->addWidget(back_);
    row->addWidget(slider_, 1);
    row->addWidget(forward_);
    row->addWidget(label_);
    row->addWidget(live_);
    connect(back_, &QToolButton::clicked, this, [this] { step(-1); });
    connect(forward_, &QToolButton::clicked, this, [this] { step(+1); });
    connect(slider_, &QSlider::sliderPressed, this, [this] { emit scrubStarted(); });
    connect(slider_, &QSlider::valueChanged, this, [this](int v) { // a drag, a click on the groove, a key
        if (showingPast() && v == position()) return;
        if (!showingPast()) emit scrubStarted();
        seek(v);
    });
    connect(live_, &QPushButton::clicked, this, &LabTimeline::goLive);
    view_->installEventFilter(this);
    refresh();
    hide();
}

// 1. The same frame again (a paused scene publishes it after every command): nothing to keep.
// 2. A frame number that went back: a new run, the old recording goes. 3. Keep it, drop the oldest
// frames while over the capacity or the memory budget.
void LabTimeline::record(const Frame& s) {
    if (!s) return;
    if (!frames_.empty() && s->frame == frames_.back()->frame && s->sceneName == frames_.back()->sceneName) return;
    if (!frames_.empty() && (s->frame < frames_.back()->frame || s->sceneName != frames_.back()->sceneName)) clear();
    const size_t size = approxBytes(*s);
    frames_.push_back(s);
    sizes_.push_back(size);
    bytes_ += size;
    while (frames_.size() > 1 && (int(frames_.size()) > capacity_ || bytes_ > kBudgetBytes)) dropOldest();
    if (isVisible()) refresh();
}

// The oldest kept frame goes, and what it cost with it.
void LabTimeline::dropOldest() {
    bytes_ -= sizes_.front();
    frames_.pop_front();
    sizes_.pop_front();
    if (shown_ > 0) --shown_; // the frame on screen keeps its place in the ring
}

// Nothing kept; the view is live.
void LabTimeline::clear() {
    frames_.clear();
    sizes_.clear();
    bytes_ = 0;
    shown_ = -1;
    refresh();
}

// How many frames to keep (two at least: a scrubber needs somewhere to go).
void LabTimeline::setCapacity(int frames) {
    capacity_ = std::max(2, frames);
    while (int(frames_.size()) > capacity_) dropOldest();
    refresh();
}

LabTimeline::Frame LabTimeline::frameAt(int index) const {
    return index >= 0 && index < count() ? frames_[size_t(index)] : nullptr;
}

// Show kept frame `index` (clamped to the ring): the view draws it exactly as it was recorded.
void LabTimeline::seek(int index) {
    if (frames_.empty()) return;
    index = std::clamp(index, 0, count() - 1);
    shown_ = index;
    emit showFrame(frames_[size_t(index)]);
    refresh();
}

// ◀ and ▶: one frame back or on; the first step away from the newest frame pauses the scene.
void LabTimeline::step(int delta) {
    if (frames_.empty()) return;
    if (!showingPast()) emit scrubStarted();
    seek(position() + delta);
}

// «Вживую»: the newest frame on screen again, and the scene runs on if the timeline paused it.
void LabTimeline::goLive() {
    const bool wasPast = showingPast();
    shown_ = -1;
    if (!frames_.empty()) emit showFrame(frames_.back());
    refresh();
    if (wasPast) emit wentLive();
}

// The slider spans the kept frames; the words say which frame, its time and what the ring costs.
void LabTimeline::refresh() {
    slider_->blockSignals(true);
    slider_->setRange(0, std::max(0, count() - 1));
    slider_->setValue(position());
    slider_->blockSignals(false);
    const bool any = !frames_.empty();
    slider_->setEnabled(count() > 1);
    back_->setEnabled(any && position() > 0);
    forward_->setEnabled(any && showingPast() && position() < count() - 1);
    live_->setEnabled(showingPast());
    const QString memory = QString::number(double(bytes_) / double(1 << 20), 'f', 1).replace('.', ',') + " МБ";
    if (!any) label_->setText("Запись начнётся с ▶ Пуск");
    else if (showingPast())
        label_->setText(QString("Кадр %1 из %2 · t = %3 с · %4")
                            .arg(position() + 1).arg(count())
                            .arg(QString::number(frames_[size_t(position())]->time, 'f', 2).replace('.', ',')).arg(memory));
    else label_->setText(QString("В записи %1 · %2").arg(framesWord(count()), memory));
    live_->setProperty("past", showingPast());
    live_->style()->unpolish(live_);
    live_->style()->polish(live_);
}

// The view changed its size: the strip keeps to its bottom edge.
bool LabTimeline::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_ && event->type() == QEvent::Resize) place();
    return QFrame::eventFilter(watched, event);
}

void LabTimeline::showEvent(QShowEvent* e) {
    QFrame::showEvent(e);
    refresh();
    place();
}

void LabTimeline::hideEvent(QHideEvent* e) {
    QFrame::hideEvent(e);
    emit placed(0);
}

// Along the bottom of the view, 12 px from its edges, above the picture; the play buttons move up
// by the strip's height (the placed signal).
void LabTimeline::place() {
    const int h = sizeHint().height();
    setGeometry(12, view_->height() - h - 12, std::max(200, view_->width() - 24), h);
    raise();
    emit placed(isVisible() ? h + 12 : 0);
}

// What one kept frame costs: the snapshot itself and every list it holds (a shared obstacle mesh is
// counted by no frame - all of them point at the one copy).
size_t LabTimeline::approxBytes(const rf::RenderSnapshot& s) {
    size_t n = sizeof(rf::RenderSnapshot);
    n += s.bodies.size() * sizeof(rf::RenderSnapshot::Body);
    n += (s.particles.size() + s.particleColor.size() + s.liquid.size()) * sizeof(rf::Vector3) + s.particleScalar.size() * sizeof(float);
    for (const auto& c : s.cloths) n += c.positions.size() * sizeof(rf::Vector3) + c.cellIntact.size() + c.burnt.size() * sizeof(float);
    for (const auto& m : s.softMeshes) n += m.positions.size() * sizeof(rf::Vector3) + m.triangles.size() * 3 * sizeof(uint32_t);
    n += s.slice.size() * sizeof(float) + s.sliceSolid.size() + s.volume.size();
    for (const auto& l : s.fieldLines) n += l.size() * sizeof(rf::Vector3);
    for (const auto& l : s.streamlines) n += l.size() * sizeof(rf::Vector3);
    n += (s.arrowPos.size() + s.arrowVel.size()) * sizeof(rf::Vector3);
    n += s.probe.lines.size() * sizeof(rf::Probe::Line) + s.probe.points.size() * sizeof(rf::Probe::Point);
    n += s.probe.labels.size() * sizeof(rf::Probe::Label) + s.probe.channels.size() * sizeof(rf::Probe::Channel);
    n += s.contacts.size() * sizeof(rf::RenderSnapshot::ContactInfo);
    return n;
}
