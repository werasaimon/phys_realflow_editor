// The "Группа" and "Массив" cards of the inspector (see ManyPanels.h).
#include "ManyPanels.h"
#include "RuPlural.h"

#include "Icons.h"
#include "InspectorWidgets.h"

#include <QBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>

using namespace rf;

// ---------------------------------------------------------------------------
// Группа
// ---------------------------------------------------------------------------
GroupPanel::GroupPanel(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    members_ = new QLabel;
    members_->setObjectName("roleRowLabel");
    col->addWidget(members_);
    glue_ = new QPushButton("Склеить в одно тело");
    glue_->setObjectName("glueButton");
    glue_->setCheckable(true);
    glue_->setToolTip("Твёрдые части группы станут одним телом: падают и катятся вместе, как склеенные");
    connect(glue_, &QPushButton::toggled, this, &GroupPanel::gluedToggled);
    auto* ungroup = new QPushButton("Разгруппировать");
    ungroup->setToolTip("Ctrl+Shift+G: объекты останутся на своих местах, группа исчезнет");
    connect(ungroup, &QPushButton::clicked, this, &GroupPanel::ungroupClicked);
    auto* row = new QHBoxLayout;
    row->addWidget(glue_);
    row->addWidget(ungroup);
    col->addLayout(row);
}

void GroupPanel::setGroup(const Group& g, int members) {
    const QSignalBlocker quiet(glue_);
    members_->setText("Внутри " + ruPlural(members, "объект", "объекта", "объектов") +
                      ". Щелчок по объекту в сцене выбирает группу, ещё щелчок — сам объект.");
    members_->setWordWrap(true);
    glue_->setChecked(g.glued);
    glue_->setText(g.glued ? "Склеено в одно тело" : "Склеить в одно тело");
}

// ---------------------------------------------------------------------------
// Массив
// ---------------------------------------------------------------------------
ArrayPanel::ArrayPanel(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    auto* patterns = new QHBoxLayout;
    const ObjectIcon icons[] = {ObjectIcon::ArrayLine, ObjectIcon::ArrayGrid, ObjectIcon::ArrayCircle};
    const char* names[] = {"Ряд", "Сетка", "Круг"};
    for (int k = 0; k < 3; ++k) {
        auto* b = new QToolButton;
        b->setObjectName(QString("arrayPattern%1").arg(k));
        b->setIcon(objectIcon(icons[k], 32));
        b->setIconSize(QSize(32, 32));
        b->setText(names[k]);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setCheckable(true);
        b->setAutoRaise(true);
        const ArrayPattern p = ArrayPattern(k);
        connect(b, &QToolButton::clicked, this, [this, p] { choosePattern(p); });
        patterns_[k] = b;
        patterns->addWidget(b);
    }
    patterns->addStretch(1);
    col->addLayout(patterns);
    form_ = new QFormLayout;
    form_->setSpacing(6);
    countRow_ = new QWidget;
    auto* counts = new QHBoxLayout(countRow_);
    counts->setContentsMargins(0, 0, 0, 0);
    for (int k = 0; k < 3; ++k) {
        counts_[k] = new QSpinBox;
        counts_[k]->setObjectName(QString("arrayCount%1").arg(k));
        counts_[k]->setRange(1, 1000);
        counts_[k]->setKeyboardTracking(false);
        connect(counts_[k], &QSpinBox::valueChanged, this, &ArrayPanel::edited);
        counts->addWidget(counts_[k], 1);
    }
    form_->addRow("Сколько", countRow_);
    step_ = new Vec3Row(-50, 50, 0.05, 3);
    form_->addRow("Шаг, м", step_);
    radius_ = makeSpin(0.01, 50, 0.1, 2);
    form_->addRow("Радиус, м", radius_);
    turn_ = new Vec3Row(-360, 360, 5, 1);
    form_->addRow("Поворот каждой, °", turn_);
    jitter_ = makeSpin(0, 5, 0.01, 3);
    jitter_->setToolTip("Каждая копия сдвинута на случайное расстояние до этого (одно и то же при каждом запуске)");
    form_->addRow("Разброс, м", jitter_);
    for (Vec3Row* r : {step_, turn_}) connect(r, &Vec3Row::edited, this, &ArrayPanel::edited);
    for (QDoubleSpinBox* s : {radius_, jitter_}) connect(s, &QDoubleSpinBox::valueChanged, this, &ArrayPanel::edited);
    col->addLayout(form_);
    template_ = new QLabel;
    template_->setObjectName("roleRowLabel");
    template_->setWordWrap(true);
    col->addWidget(template_);
    auto* explode = new QPushButton("Разобрать на объекты");
    explode->setObjectName("explodeArray");
    explode->setToolTip("Каждая копия станет отдельным объектом на своём месте");
    connect(explode, &QPushButton::clicked, this, &ArrayPanel::explodeClicked);
    col->addWidget(explode);
}

void ArrayPanel::setArray(const ArrayObject& a, const QString& templateName) {
    pattern_ = a.pattern;
    for (int k = 0; k < 3; ++k) {
        const QSignalBlocker quiet(counts_[k]);
        counts_[k]->setValue(std::max(1, a.count[k]));
        patterns_[k]->setChecked(int(a.pattern) == k);
    }
    step_->setValue(a.step);
    turn_->setValue(a.rotationStepDeg);
    for (QDoubleSpinBox* s : {radius_, jitter_}) s->blockSignals(true);
    radius_->setValue(a.radius);
    jitter_->setValue(a.jitter);
    for (QDoubleSpinBox* s : {radius_, jitter_}) s->blockSignals(false);
    template_->setText("Копии формы «" + templateName + "» (она скрыта: правьте её в списке — изменятся все копии)");
    showPatternRows();
}

void ArrayPanel::writeTo(ArrayObject& a) const {
    a.pattern = pattern_;
    for (int k = 0; k < 3; ++k) a.count[k] = counts_[k]->value();
    if (pattern_ != ArrayPattern::Grid) a.count[1] = a.count[2] = 1;
    a.step = step_->value();
    a.radius = float(radius_->value());
    a.rotationStepDeg = turn_->value();
    a.jitter = float(jitter_->value());
}

// Ряд: how many and the step between two copies; Сетка: how many along x, y, z and the spacing;
// Круг: how many and the radius.
void ArrayPanel::choosePattern(ArrayPattern p) {
    pattern_ = p;
    for (int k = 0; k < 3; ++k) patterns_[k]->setChecked(int(p) == k);
    showPatternRows();
    emit edited();
}

void ArrayPanel::showPatternRows() {
    const bool grid = pattern_ == ArrayPattern::Grid, circle = pattern_ == ArrayPattern::Circle;
    counts_[1]->setVisible(grid);
    counts_[2]->setVisible(grid);
    form_->setRowVisible(step_, !circle);
    form_->setRowVisible(radius_, circle);
    if (auto* label = qobject_cast<QLabel*>(form_->labelForField(step_))) label->setText(grid ? "Расстояние, м" : "Шаг, м");
}
