// The Laboratory's panel (see LabPanel.h): quick presets, the layers by group, the card, the profiler.
#include "LabPanel.h"

#include "LabCard.h"
#include "LabLayers.h"
#include "ProfilerBar.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

// The little colour sample at the end of a layer's row: one square per colour, or a gradient bar from
// the first colour to the last when the layer is coloured by a quantity.
class LayerSwatch : public QWidget {
public:
    LayerSwatch(const std::vector<QColor>& colors, bool gradient, QWidget* parent = nullptr)
        : QWidget(parent), colors_(colors), gradient_(gradient) {
        setFixedSize(gradient ? 34 : int(colors.size()) * 12, 12);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0, 0, 0, 120), 1.0));
        if (gradient_) {
            QLinearGradient g(0, 0, width(), 0);
            for (size_t i = 0; i < colors_.size(); ++i) g.setColorAt(double(i) / double(colors_.size() - 1), colors_[i]);
            p.setBrush(g);
            p.drawRoundedRect(QRectF(0.5, 1.5, width() - 1, height() - 3), 2, 2);
            return;
        }
        for (size_t i = 0; i < colors_.size(); ++i) {
            p.setBrush(colors_[i]);
            p.drawRoundedRect(QRectF(12.0 * double(i) + 1.5, 1.5, 9, 9), 2, 2);
        }
    }

private:
    std::vector<QColor> colors_;
    bool gradient_;
};

// A section's heading («Быстрые наборы», «Что рисовать»), styled by main.cpp's labStyle().
QLabel* sectionTitle(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("labSection");
    return l;
}

} // namespace

// Top to bottom: two lines of what this is, the presets, the layers (they take the spare height), the
// card, the profiler. The boxes start as the Probe's layers are now.
LabPanel::LabPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("labPanel");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(10, 8, 10, 10);
    col->setSpacing(8);
    auto* intro = new QLabel("Что считает движок — прямо в виде. Щёлкните по контакту или телу: "
                             "его числа появятся в карточке.");
    intro->setObjectName("labIntro");
    intro->setWordWrap(true);
    col->addWidget(intro);
    buildPresets(col);
    col->addWidget(sectionTitle("Что рисовать"));
    col->addWidget(buildLayerList(), 1);
    card_ = new LabCard;
    col->addWidget(card_);
    col->addWidget(sectionTitle("Время кадра"));
    profiler_ = new ProfilerBar;
    col->addWidget(profiler_);
    showMask(rf::Probe::layers());
}

// «Быстрые наборы»: four buttons in two rows; the one whose set is on now is lit.
void LabPanel::buildPresets(QVBoxLayout* col) {
    col->addWidget(sectionTitle("Быстрые наборы"));
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    int i = 0;
    for (const LabPreset& preset : labPresets()) {
        auto* b = new QPushButton(preset.name);
        b->setObjectName("labPreset");
        b->setCheckable(true);
        b->setToolTip(preset.tip);
        b->setCursor(Qt::PointingHandCursor);
        const QString name = preset.name;
        connect(b, &QPushButton::clicked, this, [this, name] { applyPreset(name); });
        grid->addWidget(b, i / 2, i % 2);
        presets_.push_back(b);
        ++i;
    }
    col->addLayout(grid);
}

// Every group with its layers, in a scroll area of its own: the card and the profiler stay in view.
QWidget* LabPanel::buildLayerList() {
    auto* host = new QWidget;
    host->setObjectName("labLayers");
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(0, 0, 4, 0);
    col->setSpacing(2);
    for (const QString& group : labGroups()) addGroup(col, group);
    col->addStretch(1);
    auto* scroll = new QScrollArea;
    scroll->setObjectName("labLayerScroll");
    scroll->setMinimumHeight(118); // five layers at least, however tall the card
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

// One group: its name, then a row per layer - the checkbox with its words, and its colours at the end.
void LabPanel::addGroup(QVBoxLayout* col, const QString& group) {
    auto* caption = new QLabel(group);
    caption->setObjectName("labGroup");
    col->addWidget(caption);
    for (const LabLayer& layer : labLayers()) {
        if (layer.group != group) continue;
        auto* row = new QWidget;
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(6, 0, 2, 0);
        auto* box = new QCheckBox(layer.name);
        box->setObjectName("labLayer");
        box->setToolTip(layer.tip);
        row->setToolTip(layer.tip);
        line->addWidget(box, 1);
        line->addWidget(new LayerSwatch(layer.colors, layer.gradient));
        col->addWidget(row);
        const uint32_t bit = rf::Probe::bit(layer.layer);
        connect(box, &QCheckBox::toggled, this, [this, bit](bool on) {
            if (shownMask_ == ~0u) return; // being filled from the Probe
            setMask(on ? (rf::Probe::layers() | bit) : (rf::Probe::layers() & ~bit));
        });
        boxes_[int(layer.layer)] = box;
    }
}

// A preset replaces the whole set of layers (never adds to it): what it names is exactly what is drawn.
void LabPanel::applyPreset(const QString& name) {
    for (const LabPreset& p : labPresets())
        if (p.name == name) setMask(p.mask);
}

// Layers on top of what is drawn («Следить за парой» adds GJK and EPA).
void LabPanel::addLayers(uint32_t mask) { setMask(rf::Probe::layers() | mask); }

QCheckBox* LabPanel::layerBox(rf::DrawLayer l) const {
    const auto it = boxes_.find(int(l));
    return it != boxes_.end() ? it->second : nullptr;
}

// The one place the panel changes what the engine draws: the Probe, the boxes, the signal.
void LabPanel::setMask(uint32_t mask) {
    rf::Probe::setLayers(mask);
    showMask(mask);
    emit layersChanged(mask);
}

// Someone else changed the layers (a scene, a test, «Следить за парой»): the boxes follow.
void LabPanel::syncFromProbe() {
    if (rf::Probe::layers() != shownMask_) showMask(rf::Probe::layers());
}

// The checkboxes and the presets show `mask`; while they are set their signals do not reach the Probe.
void LabPanel::showMask(uint32_t mask) {
    shownMask_ = ~0u;
    for (auto& [layer, box] : boxes_) box->setChecked((mask & rf::Probe::bit(rf::DrawLayer(layer))) != 0);
    for (size_t i = 0; i < presets_.size(); ++i) presets_[i]->setChecked(labPresets()[i].mask == mask);
    shownMask_ = mask;
}
