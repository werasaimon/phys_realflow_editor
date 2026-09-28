#pragma once
// "Мягкое тело — из чего": the material of a soft body (rf::SoftRole: a Neo-Hookean solid of
// tetrahedra, with real numbers). At a glance a row of presets - Желе, Резина, Мягкий пластик,
// Своё -: one click gives the body a real material, and the button of the material its numbers are
// stays pressed. The numbers themselves - Young's modulus E, Poisson's ratio ν, the density and the
// friction - fold away under "Подробнее" (closed at first; "Своё" opens it). Typing a number presses
// the preset the numbers now are, or "Своё" (the SDK decides: rf::softPresetOf).
#include "scene/SceneGraph.h"

#include <QWidget>

class CollapsibleSection;
class QButtonGroup;
class QDoubleSpinBox;
class QFormLayout;
class QVBoxLayout;

class SoftPanel : public QWidget {
    Q_OBJECT
public:
    explicit SoftPanel(QWidget* parent = nullptr);
    void setSoft(const rf::SoftRole& s); // does not report a change
    void writeTo(rf::SoftRole& s) const; // the four numbers; not `enabled`, not the legacy model
    void showMixedPreset();              // several selected, of different materials: no button pressed

    // The number fields, to show "—" where several selected differ (SceneBuilderMulti.cpp).
    QDoubleSpinBox* youngField() const { return young_; }
    QDoubleSpinBox* poissonField() const { return poisson_; }
    QDoubleSpinBox* densityField() const { return density_; }
    QDoubleSpinBox* frictionField() const { return friction_; }

signals:
    void presetChosen(); // a material button was clicked: the four numbers are its now
    void edited();       // the user typed or scrolled one number

private:
    void buildPresetRow(QVBoxLayout* col);
    void buildDetails(QVBoxLayout* col);
    QDoubleSpinBox* numberRow(QFormLayout* f, const QString& label, const QString& tip, double min, double max, double step,
                              int decimals);
    void onPresetClicked(int preset);
    void onNumberEdited();
    void pressMatchingPreset(); // the button of the material the numbers are, "Своё" if none

    QButtonGroup* presets_ = nullptr; // the buttons, their ids the rf::SoftPreset values
    CollapsibleSection* details_ = nullptr;
    QDoubleSpinBox *young_ = nullptr, *poisson_ = nullptr, *density_ = nullptr, *friction_ = nullptr;
    rf::SoftRole shown_; // the role as it was filled (the legacy model counts in naming the material)
    bool filling_ = false;
};
