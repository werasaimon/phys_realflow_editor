#pragma once
// The Laboratory's panel (the «Лаборатория» dock): everything the engine can show about itself, in one
// calm column, the way PhysX Visual Debugger lays it out.
//   1. «Быстрые наборы» - four buttons that switch on a whole set of layers at once.
//   2. «Что рисовать» - every debug layer of the SDK (LabLayers.h), grouped: a checkbox, its colours,
//      and a tooltip saying what it shows and how to read it.
//   3. The card of what was clicked in the view (LabCard.h).
//   4. «Время кадра» - the live profiler (ProfilerBar.h).
// A checkbox sets rf::Probe::setLayers at once; the solvers draw the layer from the next step on and
// cost nothing while it is off (the SDK's tests prove it). Nothing of this is seen until the
// Laboratory is opened: a beginner's screen stays clean.
#include "core/Probe.h"

#include <QWidget>

#include <cstdint>
#include <map>
#include <vector>

class LabCard;
class ProfilerBar;
class QCheckBox;
class QPushButton;
class QVBoxLayout;

class LabPanel : public QWidget {
    Q_OBJECT
public:
    explicit LabPanel(QWidget* parent = nullptr);

    LabCard* card() const { return card_; }
    ProfilerBar* profiler() const { return profiler_; }

    // The checkboxes follow rf::Probe::layers() when someone else changed them (the expert dock's
    // «Отладочная отрисовка», a test). Cheap: nothing happens when the mask did not change.
    void syncFromProbe();
    // Switch on exactly the layers of the preset with this name («Контакты», ...).
    void applyPreset(const QString& name);
    // Switch these layers on (the others keep their state): the card's «Следить за парой» uses it.
    void addLayers(uint32_t mask);
    QCheckBox* layerBox(rf::DrawLayer l) const;

signals:
    void layersChanged(uint32_t mask); // the set of layers drawn changed (from a checkbox or a preset)

private:
    void buildPresets(QVBoxLayout* col);
    QWidget* buildLayerList();
    void addGroup(QVBoxLayout* col, const QString& group);
    void setMask(uint32_t mask);
    void showMask(uint32_t mask); // checkboxes and the lit preset, without touching the Probe

    std::map<int, QCheckBox*> boxes_;  // by rf::DrawLayer
    std::vector<QPushButton*> presets_;
    LabCard* card_ = nullptr;
    ProfilerBar* profiler_ = nullptr;
    uint32_t shownMask_ = ~0u;         // what the checkboxes show now
};
