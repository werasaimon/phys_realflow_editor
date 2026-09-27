// The collider part of the rigid role (see ColliderPanel.h).
#include "ColliderPanel.h"

#include "Icons.h"
#include "InspectorWidgets.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QToolButton>

using namespace rf;

namespace {

const char* kTips[] = {
    "Авто: сталкивается самой формой — коробка для куба, шар для сферы, оболочка для остального",
    "Коробка: самый быстрый коллайдер; ящики, стены, всё прямоугольное",
    "Сфера: катится; мячи, камни, всё круглое",
    "Капсула: цилиндр с полусферами на концах; персонажи, бочки, бутылки, ноги коня",
    "Выпуклая оболочка: форма, натянутая как плёнка на геометрию, без вмятин",
    "Точно: геометрия, разбитая на выпуклые части — вмятины и ручки чайника тоже сталкиваются (медленнее)"};

} // namespace

QString colliderName(ColliderKind k) {
    switch (k) {
    case ColliderKind::Auto: return "Авто";
    case ColliderKind::Box: return "Коробка";
    case ColliderKind::Sphere: return "Сфера";
    case ColliderKind::Capsule: return "Капсула";
    case ColliderKind::ConvexHull: return "Выпуклая оболочка";
    case ColliderKind::Decomposition: return "Точно";
    }
    return {};
}

ColliderPanel::ColliderPanel(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 4, 0, 0);
    col->setSpacing(6);
    auto* title = new QLabel("чем тело сталкивается; виден зелёным каркасом");
    title->setObjectName("roleRowLabel");
    title->setToolTip("Как в Houdini: геометрия — то, что видно; коллайдер — то, чем тело ударяется о других.\n"
                      "Физика двигает и поворачивает геометрию вместе с коллайдером, но саму геометрию не меняет.\n"
                      "Коллайдер виден тонким зелёным каркасом поверх формы.");
    col->addWidget(title);
    buildKindRow(col);
    fit_ = new QCheckBox("Подогнать под геометрию");
    fit_->setToolTip("Размер коллайдера берётся из размеров формы; снимите, чтобы задать его руками");
    connect(fit_, &QCheckBox::toggled, this, [this] {
        updateVisibility();
        if (!filling_) emit edited();
    });
    col->addWidget(fit_);
    buildManualFields(col);
    updateVisibility();
}

// Six icon buttons, one pressed: the kind of the collider.
void ColliderPanel::buildKindRow(QVBoxLayout* col) {
    auto* row = new QHBoxLayout;
    row->setSpacing(3);
    kinds_ = new QButtonGroup(this);
    kinds_->setExclusive(true);
    for (int k = 0; k <= int(ColliderKind::Decomposition); ++k) {
        auto* b = new QToolButton;
        b->setObjectName("colliderButton");
        b->setCheckable(true);
        b->setIcon(colliderIcon(ColliderKind(k), 28));
        b->setIconSize(QSize(28, 28));
        b->setToolTip(kTips[k]);
        kinds_->addButton(b, k);
        row->addWidget(b);
    }
    row->addStretch(1);
    connect(kinds_, &QButtonGroup::idClicked, this, &ColliderPanel::onKindClicked);
    col->addLayout(row);
    kindName_ = new QLabel;
    kindName_->setObjectName("colliderKindName");
    col->addWidget(kindName_);
}

// Size, offset and turn of a collider that is not fitted to the geometry.
void ColliderPanel::buildManualFields(QVBoxLayout* col) {
    manual_ = new QWidget;
    auto* f = new QFormLayout(manual_);
    f->setContentsMargins(0, 0, 0, 0);
    f->setSpacing(6);
    size_ = new Vec3Row(0.005, 50, 0.05, 3);
    offset_ = new Vec3Row(-50, 50, 0.05, 3);
    rotation_ = new Vec3Row(-360, 360, 5, 1);
    f->addRow("Размер, м", size_);
    sizeRow_ = f->itemAt(f->rowCount() - 1, QFormLayout::LabelRole)->widget();
    f->addRow("Сдвиг, м", offset_);
    f->addRow("Поворот, °", rotation_);
    for (Vec3Row* r : {size_, offset_, rotation_})
        connect(r, &Vec3Row::edited, this, [this] {
            if (!filling_) emit edited();
        });
    col->addWidget(manual_);
}

void ColliderPanel::onKindClicked(int kind) {
    kind_ = ColliderKind(kind);
    updateVisibility();
    if (!filling_) emit edited();
}

// Auto uses the geometry as it is: no fit, no numbers. A hull and the convex parts take their
// size from the geometry, so only the offset and the turn can be typed for them.
void ColliderPanel::updateVisibility() {
    const bool automatic = kind_ == ColliderKind::Auto;
    const bool sized = kind_ == ColliderKind::Box || kind_ == ColliderKind::Sphere || kind_ == ColliderKind::Capsule;
    kindName_->setText(automatic ? (autoResult_.isEmpty() ? "Авто — по форме геометрии" : "Авто → " + autoResult_) : colliderName(kind_));
    fit_->setVisible(!automatic);
    manual_->setVisible(!automatic && !fit_->isChecked());
    size_->setVisible(sized);
    if (sizeRow_) sizeRow_->setVisible(sized);
}

void ColliderPanel::setAutoResult(const QString& what) {
    autoResult_ = what;
    updateVisibility();
}

QString collisionShapeName(ShapeType t) {
    switch (t) {
    case ShapeType::Box: return "коробка";
    case ShapeType::Sphere: return "сфера";
    case ShapeType::Capsule: return "капсула";
    case ShapeType::ConvexHull: return "выпуклая оболочка";
    case ShapeType::Compound: return "выпуклые части";
    case ShapeType::Triangle: break;
    }
    return "?";
}

void ColliderPanel::setCollider(const ColliderRole& c) {
    filling_ = true;
    kind_ = c.kind;
    if (QAbstractButton* b = kinds_->button(int(c.kind))) b->setChecked(true);
    fit_->setChecked(c.fitToGeometry);
    size_->setValue(c.size);
    offset_->setValue(c.offset);
    rotation_->setValue(c.rotationDeg);
    updateVisibility();
    filling_ = false;
}

void ColliderPanel::writeTo(ColliderRole& c) const {
    c.kind = kind_;
    c.fitToGeometry = fit_->isChecked();
    c.size = size_->value();
    c.offset = offset_->value();
    c.rotationDeg = rotation_->value();
}
