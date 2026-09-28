// The material of a soft body (see SoftPanel.h).
#include "SoftPanel.h"

#include "InspectorWidgets.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QFormLayout>
#include <QLabel>
#include <QToolButton>

using namespace rf;

namespace {

// Young's modulus the way a person reads it: "15 кПа", "1 МПа", "5 МПа".
QString pascals(double e) {
    if (e >= 1e6) return QString::number(e / 1e6, 'g', 3) + " МПа";
    if (e >= 1e3) return QString::number(e / 1e3, 'g', 3) + " кПа";
    return QString::number(e, 'g', 3) + " Па";
}

// One line under the mouse: the preset's numbers, straight from the SDK's table of materials.
QString presetTip(SoftPreset p) {
    if (p == SoftPreset::Custom) return "Свои числа: откроет «Подробнее»";
    const SoftRole m = softPreset(p);
    return QString::fromUtf8(softPresetName(p)) + ": E = " + pascals(m.youngModulus) + ", ν = " +
           QString::number(double(m.poissonRatio), 'g', 3) + ", " + QString::number(double(m.density), 'g', 5) + " кг/м³";
}

const char* kYoungTip = "Модуль Юнга: насколько тело сопротивляется растяжению; желе ~10⁴ Па, резина ~10⁶ Па";
const char* kPoissonTip = "Коэффициент Пуассона: насколько тело сохраняет объём; 0.5 — несжимаемое (резина, желе ~0.45–0.49)";
const char* kDensityTip = "Сколько весит кубометр материала: вода — 1000 кг/м³";
const char* kFrictionTip = "Коэффициент трения Кулона о пол, тела и другие мягкие тела";

} // namespace

SoftPanel::SoftPanel(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 4, 0, 0);
    col->setSpacing(6);
    auto* title = new QLabel("из чего тело: один щелчок — настоящий материал");
    title->setObjectName("roleRowLabel");
    title->setWordWrap(true);
    col->addWidget(title);
    buildPresetRow(col);
    buildDetails(col);
}

// Four buttons in one row, one of them pressed: Желе, Резина, Мягкий пластик, Своё.
void SoftPanel::buildPresetRow(QVBoxLayout* col) {
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    presets_ = new QButtonGroup(this);
    presets_->setExclusive(true);
    for (SoftPreset p : {SoftPreset::Jelly, SoftPreset::Rubber, SoftPreset::SoftPlastic, SoftPreset::Custom}) {
        auto* b = new QToolButton;
        b->setObjectName("softPreset");
        b->setCheckable(true);
        b->setText(QString::fromUtf8(softPresetName(p)));
        b->setToolTip(presetTip(p));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        presets_->addButton(b, int(p));
        row->addWidget(b);
    }
    connect(presets_, &QButtonGroup::idClicked, this, &SoftPanel::onPresetClicked);
    col->addLayout(row);
}

// "Подробнее", folded at first: the four numbers of the material, each with a one-line tip.
void SoftPanel::buildDetails(QVBoxLayout* col) {
    details_ = new CollapsibleSection("Подробнее");
    details_->setObjectName("softDetails");
    auto* f = new QFormLayout;
    f->setSpacing(6);
    details_->body()->addLayout(f);
    young_ = numberRow(f, "Модуль Юнга E, Па", kYoungTip, 100, 1e9, 100, 0);
    // From jelly (10⁴) to stiff plastic (10⁹): the wheel and the arrow keys step a tenth of the
    // number's own order (15 000 -> 16 000, 1 000 000 -> 1 100 000), and the digits are grouped.
    young_->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    young_->setGroupSeparatorShown(true);
    poisson_ = numberRow(f, "Коэффициент Пуассона ν", kPoissonTip, 0.05, 0.49, 0.01, 2);
    density_ = numberRow(f, "Плотность, кг/м³", kDensityTip, 1, 5000, 10, 0);
    friction_ = numberRow(f, "Трение", kFrictionTip, 0, 2, 0.05, 2);
    col->addWidget(details_);
}

QDoubleSpinBox* SoftPanel::numberRow(QFormLayout* f, const QString& label, const QString& tip, double min, double max,
                                     double step, int decimals) {
    QDoubleSpinBox* s = makeSpin(min, max, step, decimals);
    s->setToolTip(tip);
    auto* name = new QLabel(label);
    name->setToolTip(tip);
    f->addRow(name, s);
    connect(s, &QDoubleSpinBox::valueChanged, this, &SoftPanel::onNumberEdited);
    return s;
}

// A material: its four numbers into the fields, reported once. "Своё" changes nothing: it only
// opens the numbers, to be typed.
void SoftPanel::onPresetClicked(int preset) {
    if (SoftPreset(preset) == SoftPreset::Custom) return details_->setOpen(true);
    const SoftRole m = softPreset(SoftPreset(preset));
    filling_ = true; // four numbers, one edit
    young_->setValue(m.youngModulus);
    poisson_->setValue(m.poissonRatio);
    density_->setValue(m.density);
    friction_->setValue(m.friction);
    filling_ = false;
    pressMatchingPreset();
    emit presetChosen();
}

void SoftPanel::onNumberEdited() {
    if (filling_) return;
    pressMatchingPreset();
    emit edited();
}

// The numbers decide which button is pressed, the way the SDK names a material.
void SoftPanel::pressMatchingPreset() {
    SoftRole now = shown_;
    writeTo(now);
    presets_->button(int(softPresetOf(now)))->setChecked(true);
}

void SoftPanel::setSoft(const SoftRole& s) {
    shown_ = s;
    filling_ = true;
    young_->setValue(s.youngModulus);
    poisson_->setValue(s.poissonRatio);
    density_->setValue(s.density);
    friction_->setValue(s.friction);
    filling_ = false;
    pressMatchingPreset();
}

void SoftPanel::writeTo(SoftRole& s) const {
    s.youngModulus = float(young_->value());
    s.poissonRatio = float(poisson_->value());
    s.density = float(density_->value());
    s.friction = float(friction_->value());
}

void SoftPanel::showMixedPreset() {
    presets_->setExclusive(false); // an exclusive group never lets the last pressed button go
    for (QAbstractButton* b : presets_->buttons()) b->setChecked(false);
    presets_->setExclusive(true);
}
